#include "market_data_handler.h"
#include <chrono>
#include <random>
#include <iostream>

namespace hft {

MarketDataHandler::MarketDataHandler()
    : running_(false)
    , messages_processed_(0)
    , bytes_processed_(0)
    , logger_("MarketDataHandler") {
}

MarketDataHandler::~MarketDataHandler() {
    stop();
}

bool MarketDataHandler::initialize() {
    logger_.info("Initializing Market Data Handler");
    
    config_ = std::make_unique<Config>();
    
    try {
        // Initialize ZeroMQ context and publisher
        context_ = std::make_unique<zmq::context_t>(1);
        publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        
        // Set socket options for high performance
        int sndhwm = 1000;  // Send high water mark
        publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        
        int linger = 0;  // Don't wait for messages to be sent on close
        publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        // Bind to market data endpoint
        std::string endpoint = config_->get_string(GlobalConfig::MARKET_DATA_ENDPOINT);
        publisher_->bind(endpoint);
        
        logger_.info("Bound to endpoint: " + endpoint);
        
        // Initialize DPDK if enabled
        if (config_->get_bool(GlobalConfig::ENABLE_DPDK)) {
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
    
    // Start processing thread
    processing_thread_ = std::make_unique<std::thread>(&MarketDataHandler::process_market_data, this);
    
    logger_.info("Market Data Handler started");
}

void MarketDataHandler::stop() {
    if (!running_.load()) {
        return;
    }
    
    logger_.info("Stopping Market Data Handler");
    running_.store(false);
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    // Close ZeroMQ socket
    if (publisher_) {
        publisher_->close();
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
        if (config_->get_bool(GlobalConfig::ENABLE_DPDK)) {
            // Process DPDK packets if available
            if (!process_dpdk_packets()) {
                // Fall back to mock data if no packets
                generate_mock_data();
            }
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
        if (!config_->get_bool(GlobalConfig::ENABLE_DPDK)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    logger_.info("Market data processing thread stopped");
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
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> price_dist(100.0, 500.0);
    static std::uniform_int_distribution<> size_dist(100, 10000);
    static std::uniform_real_distribution<> spread_dist(0.01, 0.10);
    
    // Common symbols for testing
    static const std::vector<std::string> symbols = {
        "AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "NVDA", "META", "NFLX"
    };
    
    static std::uniform_int_distribution<> symbol_dist(0, symbols.size() - 1);
    
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
    
    publish_market_data(data);
}

void MarketDataHandler::publish_market_data(const MarketData& data) {
    try {
        zmq::message_t message(sizeof(MarketData));
        std::memcpy(message.data(), &data, sizeof(MarketData));
        
        publisher_->send(message, zmq::send_flags::dontwait);
        
        messages_processed_++;
        bytes_processed_ += sizeof(MarketData);
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {  // EAGAIN is expected with dontwait
            logger_.error("Failed to publish market data: " + std::string(e.what()));
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