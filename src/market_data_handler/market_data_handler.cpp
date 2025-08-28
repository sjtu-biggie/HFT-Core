#include "market_data_handler.h"

#include "../common/static_config.h"
#include "../common/hft_metrics.h"
#include "../common/metrics_collector.h"
#include "../common/metrics_publisher.h"

#include <chrono>
#include <random>
#include <iostream>

namespace hft {

MarketDataHandler::MarketDataHandler()
    : running_(false)
    , paused_(false)
    , messages_processed_(0)
    , bytes_processed_(0)
    , logger_("MarketDataHandler", StaticConfig::get_logger_endpoint()) 
    , throughput_tracker_(hft::metrics::MD_MESSAGES_RECEIVED, hft::metrics::MD_MESSAGES_PER_SEC)
    , metrics_publisher_("MarketDataHandler", "tcp://*:5562") {
}

MarketDataHandler::~MarketDataHandler() {
    stop();
}

bool MarketDataHandler::initialize() {
    logger_.info("Initializing Market Data Handler");
    
    MetricsCollector::instance().initialize();
    StaticConfig::load_from_file("config/hft_config.conf");

    // Initialize metrics publisher
    if (!metrics_publisher_.initialize()) {
        logger_.error("Failed to initialize metrics publisher");
        return false;
    }
    
    try {
        // Initialize ZeroMQ context and sockets
        context_ = std::make_unique<zmq::context_t>(1);
        publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        control_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        
        // Set socket options for high performance
        int sndhwm = 1000;  // Send high water mark
        publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        
        int linger = 0;  // Don't wait for messages to be sent on close
        publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        // Bind to market data endpoint
        const char* endpoint = StaticConfig::get_market_data_endpoint();
        publisher_->bind(endpoint);
        
        logger_.info("Bound to market data endpoint: " + std::string(endpoint));
        
        // Subscribe to control messages
        control_subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        int rcvhwm = 100;
        control_subscriber_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        control_subscriber_->connect("tcp://localhost:5570");
        logger_.info("Connected to control endpoint: tcp://localhost:5570");
        
        // Initialize DPDK if enabled
        if (StaticConfig::get_enable_dpdk()) {
            if (!initialize_dpdk()) {
                logger_.warning("DPDK initialization failed, using mock data");
            }
        }
        
        return true;
        
    } catch (const zmq::error_t& e) {
        logger_.error("ZeroMQ initialization failed: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logger_.error("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void MarketDataHandler::start() {
    if (running_.load()) {
        logger_.warning("Market Data Handler is already running");
        return;
    }
    
    logger_.info("Starting Market Data Handler");
    running_.store(true);
    
    // Start metrics publisher
    metrics_publisher_.start();
    
    // Start processing threads
    processing_thread_ = std::make_unique<std::thread>(&MarketDataHandler::process_market_data, this);
    control_thread_ = std::make_unique<std::thread>(&MarketDataHandler::process_control_messages, this);
    
    logger_.info("Market Data Handler started");
}

void MarketDataHandler::stop() {
    if (!running_.load()) {
        return;
    }
    
    logger_.info("Stopping Market Data Handler");
    running_.store(false);
    
    // Stop metrics publisher
    metrics_publisher_.stop();
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    if (control_thread_ && control_thread_->joinable()) {
        control_thread_->join();
    }
    
    // Close ZeroMQ sockets
    if (publisher_) {
        publisher_->close();
    }
    if (control_subscriber_) {
        control_subscriber_->close();
    }
    
    log_statistics();
    logger_.info("Market Data Handler stopped");
}

bool MarketDataHandler::is_running() const {
    return running_.load();
}

void MarketDataHandler::process_market_data() {
    logger_.info("Market data processing thread started");
    
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(10);
    
    while (running_.load()) {
        // Check if paused
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        if (StaticConfig::get_enable_dpdk()) {
            // Process DPDK packets if available
            process_dpdk_packets();
        } else {
            // Use mock data for testing
            generate_mock_data();
        }
        
        // Log statistics periodically
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            log_statistics();
            last_stats_time = now;
        }
        
        // Small sleep to prevent busy waiting in mock mode
        if (!StaticConfig::get_enable_dpdk()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    logger_.info("Market data processing thread stopped");
}

void MarketDataHandler::process_control_messages() {
    logger_.info("Control message processing thread started");
    
    while (running_.load()) {
        try {
            zmq::message_t message;
            if (control_subscriber_->recv(message, zmq::recv_flags::dontwait)) {
                if (message.size() == sizeof(ControlCommand)) {
                    ControlCommand command;
                    std::memcpy(&command, message.data(), sizeof(ControlCommand));
                    
                    // Check if this command is for us
                    std::string target(command.target_service);
                    if (target == "MarketDataHandler" || target == "all") {
                        handle_control_command(command);
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
        } catch (const std::exception& e) {
            logger_.error("Control message processing error: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logger_.info("Control message processing thread stopped");
}

void MarketDataHandler::handle_control_command(const ControlCommand& command) {
    switch (command.action) {
        case ControlAction::START_TRADING:
            if (paused_.load()) {
                paused_.store(false);
                logger_.info("Market data processing resumed via control command");
            } else {
                logger_.info("Market data processing already running");
            }
            break;
            
        case ControlAction::STOP_TRADING:
        case ControlAction::PAUSE_TRADING:
            if (!paused_.load()) {
                paused_.store(true);
                logger_.info("Market data processing paused via control command");
            } else {
                logger_.info("Market data processing already paused");
            }
            break;
            
        default:
            logger_.warning("Unsupported control action: " + std::to_string(static_cast<int>(command.action)));
            break;
    }
}

bool MarketDataHandler::initialize_dpdk() {
    // This is a proof-of-concept placeholder for DPDK initialization
    // In a real implementation, this would:
    // 1. Initialize DPDK EAL (Environment Abstraction Layer)
    // 2. Configure memory pools
    // 3. Set up network interfaces
    // 4. Configure packet processing
    
    logger_.info("DPDK initialization - proof of concept");
    logger_.warning("DPDK functionality is not fully implemented in this phase");
    
    // For now, return false to indicate DPDK is not available
    // This will cause the system to use mock data
    return false;
}

bool MarketDataHandler::process_dpdk_packets() {
    HFT_RDTSC_TIMER(hft::metrics::MD_TOTAL_LATENCY);
    
    // This is a proof-of-concept placeholder for DPDK packet processing
    // In a real implementation, this would:
    // 1. Poll network interface for new packets
    // 2. Parse raw ethernet/IP/UDP packets
    // 3. Extract market data from packet payload
    // 4. Convert to internal MarketData format
    
    // For now, return false to indicate no packets processed
    return false;
}

void MarketDataHandler::generate_mock_data() {
    HFT_RDTSC_TIMER(hft::metrics::MD_TOTAL_LATENCY);
    
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> price_dist(100.0, 500.0);
    static std::uniform_int_distribution<> size_dist(100, 10000);
    static std::uniform_real_distribution<> spread_dist(0.01, 0.10);
    
    // Common symbols for testing
    static const std::vector<std::string> symbols = {
        "AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "NVDA", "META", "NFLX",
        "SPY", "QQQ", "IWM", "GLD", "TLT", "VIX", "TQQQ", "SQQQ"
    };
    
    static std::uniform_int_distribution<> symbol_dist(0, symbols.size() - 1);
    
    // Track data generation latency
    {
        HFT_RDTSC_TIMER(hft::metrics::MD_PARSE_LATENCY);
        
        // Generate market data for a random symbol
        std::string symbol = symbols[symbol_dist(gen)];
        double mid_price = price_dist(gen);
        double spread = spread_dist(gen);
        
        double bid_price = mid_price - spread / 2.0;
        double ask_price = mid_price + spread / 2.0;
        uint32_t bid_size = size_dist(gen);
        uint32_t ask_size = size_dist(gen);
        
        // Create last trade
        double last_price = bid_price + (ask_price - bid_price) * 
                           std::uniform_real_distribution<>(0.0, 1.0)(gen);
        uint32_t last_size = std::uniform_int_distribution<>(100, 1000)(gen);
        
        MarketData data = MessageFactory::create_market_data(
            symbol, bid_price, ask_price, bid_size, ask_size, last_price, last_size
        );
        
        // Track message generation metrics
        HFT_COMPONENT_COUNTER(hft::metrics::MD_MESSAGES_PROCESSED);
        throughput_tracker_.increment();
        
        publish_market_data(data);
    }
}

void MarketDataHandler::publish_market_data(const MarketData& data) {
    HFT_RDTSC_TIMER(hft::metrics::MD_PUBLISH_LATENCY);
    
    try {
        zmq::message_t message(sizeof(MarketData));
        std::memcpy(message.data(), &data, sizeof(MarketData));
        
        {
            publisher_->send(message, zmq::send_flags::dontwait);
        }
        
        messages_processed_++;
        bytes_processed_ += sizeof(MarketData);
        
        // Update HFT metrics
        HFT_COMPONENT_COUNTER(hft::metrics::MD_MESSAGES_PUBLISHED);
        HFT_GAUGE_VALUE(hft::metrics::MD_BYTES_RECEIVED, bytes_processed_.load());
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {  // EAGAIN is expected with dontwait
            logger_.error("Failed to publish market data: " + std::string(e.what()));
            HFT_COMPONENT_COUNTER(hft::metrics::MD_MESSAGES_DROPPED);
        }
    }
}



void MarketDataHandler::log_statistics() {
    uint64_t msg_count = messages_processed_.load();
    uint64_t byte_count = bytes_processed_.load();
    
    std::string stats = "Processed " + std::to_string(msg_count) + 
                       " messages, " + std::to_string(byte_count) + " bytes";
    logger_.info(stats);
}

} // namespace hft