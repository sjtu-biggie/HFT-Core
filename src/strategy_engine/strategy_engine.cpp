#include "strategy_engine.h"
#include <chrono>
#include <iostream>

namespace hft {

// MomentumStrategy Implementation
MomentumStrategy::MomentumStrategy(uint64_t strategy_id)
    : strategy_id_(strategy_id)
    , logger_("MomentumStrategy") {
    logger_.info("Initialized with ID: " + std::to_string(strategy_id));
}

void MomentumStrategy::on_market_data(const MarketData& data) {
    std::string symbol(data.symbol);
    double mid_price = (data.bid_price + data.ask_price) / 2.0;
    
    auto now = std::chrono::steady_clock::now();
    
    // Check if we have previous price data
    auto price_it = last_prices_.find(symbol);
    if (price_it != last_prices_.end()) {
        double price_change = (mid_price - price_it->second) / price_it->second;
        
        // Check if enough time has passed since last signal
        auto time_it = last_signal_time_.find(symbol);
        bool can_signal = (time_it == last_signal_time_.end()) ||
                         (std::chrono::duration_cast<std::chrono::milliseconds>(
                             now - time_it->second).count() >= MIN_SIGNAL_INTERVAL_MS);
        
        if (can_signal && std::abs(price_change) > MOMENTUM_THRESHOLD) {
            // Generate momentum signal
            SignalAction action = (price_change > 0) ? SignalAction::BUY : SignalAction::SELL;
            
            TradingSignal signal = MessageFactory::create_trading_signal(
                symbol, action, OrderType::MARKET, 0.0, 100, strategy_id_, 
                std::min(std::abs(price_change) / MOMENTUM_THRESHOLD, 1.0)
            );
            
            // This would normally be sent to the strategy engine's publisher
            logger_.info("Generated " + std::string(action == SignalAction::BUY ? "BUY" : "SELL") +
                        " signal for " + symbol + " (change: " + 
                        std::to_string(price_change * 100) + "%)");
            
            last_signal_time_[symbol] = now;
        }
    }
    
    // Update last price
    last_prices_[symbol] = mid_price;
}

void MomentumStrategy::on_execution(const OrderExecution& execution) {
    std::string symbol(execution.symbol);
    logger_.info("Execution for " + symbol + ": " + 
                std::to_string(execution.fill_quantity) + " @ " +
                std::to_string(execution.fill_price));
}

// StrategyEngine Implementation
StrategyEngine::StrategyEngine()
    : running_(false)
    , market_data_processed_(0)
    , signals_generated_(0)
    , logger_("StrategyEngine") {
}

StrategyEngine::~StrategyEngine() {
    stop();
}

bool StrategyEngine::initialize() {
    logger_.info("Initializing Strategy Engine");
    
    config_ = std::make_unique<Config>();
    
    try {
        // Initialize ZeroMQ context and sockets
        context_ = std::make_unique<zmq::context_t>(1);
        
        // Market data subscriber
        subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);  // Subscribe to all messages
        int rcvhwm = 1000;
        subscriber_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        
        // Execution subscriber
        execution_sub_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        execution_sub_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        execution_sub_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        
        // Signal publisher
        signal_pub_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUSH);
        int sndhwm = 1000;
        signal_pub_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        int linger = 0;
        signal_pub_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        // Connect to endpoints
        std::string market_data_endpoint = config_->get_string(GlobalConfig::MARKET_DATA_ENDPOINT);
        subscriber_->connect(market_data_endpoint);
        logger_.info("Connected to market data: " + market_data_endpoint);
        
        // For now, use a separate endpoint for executions (in production, this would be different)
        execution_sub_->connect("tcp://localhost:5557");
        
        // Bind signal publisher
        signal_pub_->bind("tcp://localhost:5558");
        logger_.info("Signal publisher bound to tcp://localhost:5558");
        
        // Add default momentum strategy
        add_strategy(std::make_unique<MomentumStrategy>(1001));
        
        return true;
        
    } catch (const zmq::error_t& e) {
        logger_.error("ZeroMQ initialization failed: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logger_.error("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void StrategyEngine::start() {
    if (running_.load()) {
        logger_.warning("Strategy Engine is already running");
        return;
    }
    
    logger_.info("Starting Strategy Engine with " + std::to_string(strategies_.size()) + " strategies");
    running_.store(true);
    
    // Start processing thread
    processing_thread_ = std::make_unique<std::thread>(&StrategyEngine::process_messages, this);
    
    logger_.info("Strategy Engine started");
}

void StrategyEngine::stop() {
    if (!running_.load()) {
        return;
    }
    
    logger_.info("Stopping Strategy Engine");
    running_.store(false);
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    // Close sockets
    if (subscriber_) subscriber_->close();
    if (execution_sub_) execution_sub_->close();
    if (signal_pub_) signal_pub_->close();
    
    log_statistics();
    logger_.info("Strategy Engine stopped");
}

bool StrategyEngine::is_running() const {
    return running_.load();
}

void StrategyEngine::add_strategy(std::unique_ptr<Strategy> strategy) {
    logger_.info("Adding strategy: " + strategy->get_name() + 
                " (ID: " + std::to_string(strategy->get_id()) + ")");
    strategies_.push_back(std::move(strategy));
}

void StrategyEngine::process_messages() {
    logger_.info("Strategy processing thread started");
    
    // Set up polling for multiple sockets
    zmq::pollitem_t items[] = {
        { *subscriber_, 0, ZMQ_POLLIN, 0 },
        { *execution_sub_, 0, ZMQ_POLLIN, 0 }
    };
    
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(30);
    
    while (running_.load()) {
        try {
            // Poll with timeout
            zmq::poll(&items[0], 2, std::chrono::milliseconds(100));
            
            // Handle market data
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                if (subscriber_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(MarketData)) {
                        MarketData data;
                        std::memcpy(&data, message.data(), sizeof(MarketData));
                        handle_market_data(data);
                    }
                }
            }
            
            // Handle executions
            if (items[1].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                if (execution_sub_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(OrderExecution)) {
                        OrderExecution execution;
                        std::memcpy(&execution, message.data(), sizeof(OrderExecution));
                        handle_execution(execution);
                    }
                }
            }
            
            // Log statistics periodically
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= stats_interval) {
                log_statistics();
                last_stats_time = now;
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EINTR) {
                logger_.error("Message processing error: " + std::string(e.what()));
            }
        }
    }
    
    logger_.info("Strategy processing thread stopped");
}

void StrategyEngine::handle_market_data(const MarketData& data) {
    // Forward to all strategies
    for (auto& strategy : strategies_) {
        strategy->on_market_data(data);
    }
    
    market_data_processed_++;
}

void StrategyEngine::handle_execution(const OrderExecution& execution) {
    // Forward to all strategies
    for (auto& strategy : strategies_) {
        strategy->on_execution(execution);
    }
}

void StrategyEngine::publish_signal(const TradingSignal& signal) {
    try {
        zmq::message_t message(sizeof(TradingSignal));
        std::memcpy(message.data(), &signal, sizeof(TradingSignal));
        
        signal_pub_->send(message, zmq::send_flags::dontwait);
        signals_generated_++;
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {
            logger_.error("Failed to publish signal: " + std::string(e.what()));
        }
    }
}

void StrategyEngine::log_statistics() {
    uint64_t data_count = market_data_processed_.load();
    uint64_t signal_count = signals_generated_.load();
    
    std::string stats = "Processed " + std::to_string(data_count) + 
                       " market data messages, generated " + std::to_string(signal_count) + " signals";
    logger_.info(stats);
}

} // namespace hft