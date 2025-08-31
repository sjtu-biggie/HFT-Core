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
    , metrics_publisher_("MarketDataHandler", "tcp://*:5562")
    , price_generator_(std::random_device{}())
    , price_change_dist_(0.0, 0.01) // Mean 0, std dev 1% for price changes
    , session_start_time_(std::chrono::steady_clock::now()) {
    
    // Initialize symbol base prices and volatilities
    symbol_prices_["AAPL"] = 175.0; symbol_volatilities_["AAPL"] = 0.25;
    symbol_prices_["GOOGL"] = 140.0; symbol_volatilities_["GOOGL"] = 0.28;
    symbol_prices_["MSFT"] = 380.0; symbol_volatilities_["MSFT"] = 0.22;
    symbol_prices_["TSLA"] = 250.0; symbol_volatilities_["TSLA"] = 0.45;
    symbol_prices_["AMZN"] = 145.0; symbol_volatilities_["AMZN"] = 0.30;
    symbol_prices_["NVDA"] = 900.0; symbol_volatilities_["NVDA"] = 0.35;
    symbol_prices_["META"] = 350.0; symbol_volatilities_["META"] = 0.32;
    symbol_prices_["NFLX"] = 450.0; symbol_volatilities_["NFLX"] = 0.38;
    symbol_prices_["SPY"] = 450.0; symbol_volatilities_["SPY"] = 0.15;
    symbol_prices_["QQQ"] = 380.0; symbol_volatilities_["QQQ"] = 0.20;
    symbol_prices_["IWM"] = 200.0; symbol_volatilities_["IWM"] = 0.25;
    symbol_prices_["GLD"] = 180.0; symbol_volatilities_["GLD"] = 0.18;
    symbol_prices_["TLT"] = 95.0; symbol_volatilities_["TLT"] = 0.12;
    symbol_prices_["VIX"] = 18.0; symbol_volatilities_["VIX"] = 0.80;
    symbol_prices_["TQQQ"] = 45.0; symbol_volatilities_["TQQQ"] = 0.60;
    symbol_prices_["SQQQ"] = 12.0; symbol_volatilities_["SQQQ"] = 0.60;
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
        
        // Initialize data sources based on configuration
        std::string data_source = StaticConfig::get_market_data_source();
        
        if (data_source == "pcap") {
            if (!initialize_pcap_reader()) {
                logger_.warning("PCAP initialization failed, falling back to mock data");
            }
        } else if (StaticConfig::get_enable_dpdk()) {
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
    
    // Close ZeroMQ sockets safely
    if (publisher_) {
        try {
            publisher_->close();
            publisher_.reset();
        } catch (const zmq::error_t&) {
            // Ignore close errors during shutdown
        }
    }
    if (control_subscriber_) {
        try {
            control_subscriber_->close();
            control_subscriber_.reset();
        } catch (const zmq::error_t&) {
            // Ignore close errors during shutdown
        }
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
        
        // Check data source configuration
        std::string data_source = StaticConfig::get_market_data_source();
        
        if (data_source == "pcap") {
            // PCAP file replay mode
            process_pcap_data();
        } else if (data_source == "alpaca") {
            // Alpaca API mode (to be implemented)
            logger_.warning("Alpaca data source not yet implemented, falling back to mock data");
            generate_realistic_mock_data();
        } else if (StaticConfig::get_enable_dpdk()) {
            // DPDK mode
            process_dpdk_packets();
        } else {
            // Default to enhanced realistic mock data
            generate_realistic_mock_data();
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



void MarketDataHandler::generate_realistic_mock_data() {
    HFT_RDTSC_TIMER(hft::metrics::MD_TOTAL_LATENCY);
    
    // Select symbol in round-robin fashion for better distribution
    static const std::vector<std::string> symbols = {
        "AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "NVDA", "META", "NFLX",
        "SPY", "QQQ", "IWM", "GLD", "TLT", "VIX", "TQQQ", "SQQQ"
    };
    static size_t symbol_index = 0;
    std::string symbol = symbols[symbol_index];
    symbol_index = (symbol_index + 1) % symbols.size();
    
    // Get current price and volatility for symbol
    double current_price = symbol_prices_[symbol];
    double volatility = symbol_volatilities_[symbol];
    
    // Apply market session volatility multiplier
    double session_vol_multiplier = get_market_session_volatility();
    double effective_volatility = volatility * session_vol_multiplier;
    
    // Generate realistic price change using Geometric Brownian Motion
    // dS = μ * S * dt + σ * S * dW, where dW is normally distributed
    const double dt = 0.001; // Small time step (1ms)
    const double drift = 0.0; // No long-term trend for mock data
    
    double price_change = drift * current_price * dt + 
                         effective_volatility * current_price * price_change_dist_(price_generator_) * std::sqrt(dt);
    
    // Update symbol price with bounds checking
    double new_price = current_price + price_change;
    double min_price = get_symbol_base_price(symbol) * 0.5; // Don't go below 50% of base
    double max_price = get_symbol_base_price(symbol) * 2.0; // Don't go above 200% of base
    
    new_price = std::max(min_price, std::min(max_price, new_price));
    symbol_prices_[symbol] = new_price;
    
    // Generate realistic bid/ask spread based on price and volatility
    double spread_bp = 5.0 + (effective_volatility * 100.0); // 5-50 basis points
    double spread = new_price * (spread_bp / 10000.0);
    
    double bid_price = new_price - spread / 2.0;
    double ask_price = new_price + spread / 2.0;
    
    // Generate volume based on volatility (higher vol = higher volume)
    std::uniform_int_distribution<uint32_t> volume_dist(
        static_cast<uint32_t>(1000 * (1.0 - effective_volatility)),
        static_cast<uint32_t>(5000 * (1.0 + effective_volatility))
    );
    uint32_t bid_size = volume_dist(price_generator_);
    uint32_t ask_size = volume_dist(price_generator_);
    
    // Generate last trade within spread
    std::uniform_real_distribution<double> trade_price_dist(0.2, 0.8);
    double last_price = bid_price + (ask_price - bid_price) * trade_price_dist(price_generator_);
    uint32_t last_size = std::uniform_int_distribution<uint32_t>(100, 1000)(price_generator_);
    
    // Create market data message
    MarketData data = MessageFactory::create_market_data(
        symbol, bid_price, ask_price, bid_size, ask_size, last_price, last_size
    );
    
    // Track metrics
    HFT_COMPONENT_COUNTER(hft::metrics::MD_MESSAGES_PROCESSED);
    throughput_tracker_.increment();
    
    publish_market_data(data);
}

double MarketDataHandler::get_market_session_volatility() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - session_start_time_).count();
    
    // Simulate market hours volatility patterns
    // Higher volatility at market open/close, lower during lunch
    double hour_of_day = (elapsed / 60.0) + 9.5; // Start at 9:30 AM EST
    hour_of_day = std::fmod(hour_of_day, 24.0); // Wrap around day
    
    if (hour_of_day < 9.5 || hour_of_day > 16.0) {
        // Pre/post market - lower volume, higher volatility
        return 1.2;
    } else if (hour_of_day < 10.5 || hour_of_day > 15.0) {
        // Market open/close - high volatility
        return 1.5;
    } else if (hour_of_day > 12.0 && hour_of_day < 14.0) {
        // Lunch time - lower volatility
        return 0.7;
    } else {
        // Regular trading hours
        return 1.0;
    }
}

double MarketDataHandler::get_symbol_base_price(const std::string& symbol) const {
    auto it = symbol_prices_.find(symbol);
    if (it != symbol_prices_.end()) {
        return it->second;
    }
    return 100.0; // Default fallback price
}

bool MarketDataHandler::initialize_pcap_reader() {
    logger_.info("Initializing PCAP reader for market data");
    
    // Get PCAP file path from configuration
    std::string pcap_file = StaticConfig::get_pcap_file_path();
    std::string format_str = StaticConfig::get_pcap_format();
    bool use_dpdk = StaticConfig::get_enable_dpdk();
    
    // Convert format string to enum
    FeedFormat format = FeedFormat::GENERIC_CSV;
    if (format_str == "nasdaq_itch") {
        format = FeedFormat::NASDAQ_ITCH_5_0;
    } else if (format_str == "nyse_pillar") {
        format = FeedFormat::NYSE_PILLAR;
    } else if (format_str == "iex_tops") {
        format = FeedFormat::IEX_TOPS;
    } else if (format_str == "fix") {
        format = FeedFormat::FIX_PROTOCOL;
    }
    
    // Create PCAP reader
    pcap_reader_ = std::make_unique<PCAPReader>(pcap_file, format);
    
    if (!pcap_reader_->initialize(use_dpdk)) {
        logger_.error("Failed to initialize PCAP reader");
        pcap_reader_.reset();
        return false;
    }
    
    // Set up callback to publish market data
    pcap_reader_->set_data_callback([this](const MarketData& data) {
        // Update internal price tracking for realistic continuity
        std::string symbol(data.symbol);
        double mid_price = (data.bid_price + data.ask_price) / 2.0;
        symbol_prices_[symbol] = mid_price;
        
        // Publish the market data
        publish_market_data(data);
        
        // Update metrics
        HFT_COMPONENT_COUNTER(hft::metrics::MD_MESSAGES_PROCESSED);
        throughput_tracker_.increment();
    });
    
    // Configure replay parameters
    double replay_speed = StaticConfig::get_replay_speed();
    bool loop_replay = StaticConfig::get_loop_replay();
    
    pcap_reader_->set_replay_speed(replay_speed);
    pcap_reader_->set_loop_replay(loop_replay);
    
    logger_.info("PCAP reader initialized successfully");
    return true;
}

void MarketDataHandler::process_pcap_data() {
    if (!pcap_reader_) {
        logger_.error("PCAP reader not initialized");
        return;
    }
    
    logger_.info("Starting PCAP data processing");
    pcap_reader_->start_reading();
    
    // Monitor PCAP reader status
    while (running_.load() && pcap_reader_->is_reading()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Log statistics periodically
        static auto last_stats_log = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_log).count() >= 10) {
            logger_.info("PCAP Stats - Processed: " + std::to_string(pcap_reader_->get_packets_processed()) +
                        ", Parsed: " + std::to_string(pcap_reader_->get_packets_parsed()) +
                        ", Errors: " + std::to_string(pcap_reader_->get_parse_errors()));
            last_stats_log = now;
        }
    }
    
    logger_.info("PCAP data processing completed");
}

void MarketDataHandler::log_statistics() {
    uint64_t msg_count = messages_processed_.load();
    uint64_t byte_count = bytes_processed_.load();
    
    std::string stats = "Processed " + std::to_string(msg_count) + 
                       " messages, " + std::to_string(byte_count) + " bytes";
    logger_.info(stats);
}

} // namespace hft