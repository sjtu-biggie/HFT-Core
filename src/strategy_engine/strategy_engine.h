#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/config.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace hft {

// Abstract base class for trading strategies
class Strategy {
public:
    virtual ~Strategy() = default;
    
    // Called when new market data arrives
    virtual void on_market_data(const MarketData& data) = 0;
    
    // Called when order execution is received
    virtual void on_execution(const OrderExecution& execution) = 0;
    
    // Get strategy name
    virtual std::string get_name() const = 0;
    
    // Get strategy ID
    virtual uint64_t get_id() const = 0;
};

// Simple momentum strategy for testing
class MomentumStrategy : public Strategy {
public:
    explicit MomentumStrategy(uint64_t strategy_id);
    
    void on_market_data(const MarketData& data) override;
    void on_execution(const OrderExecution& execution) override;
    std::string get_name() const override { return "MomentumStrategy"; }
    uint64_t get_id() const override { return strategy_id_; }

private:
    uint64_t strategy_id_;
    std::unordered_map<std::string, double> last_prices_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_signal_time_;
    
    Logger logger_;
    
    // Strategy parameters
    static constexpr double MOMENTUM_THRESHOLD = 0.01;  // 1% price change
    static constexpr int MIN_SIGNAL_INTERVAL_MS = 1000;  // Minimum time between signals
};

class StrategyEngine {
public:
    StrategyEngine();
    ~StrategyEngine();
    
    // Initialize the engine
    bool initialize();
    
    // Start the engine
    void start();
    
    // Stop the engine
    void stop();
    
    // Check if engine is running
    bool is_running() const;
    
    // Add a strategy to the engine
    void add_strategy(std::unique_ptr<Strategy> strategy);

private:
    // Configuration
    std::unique_ptr<Config> config_;
    
    // ZeroMQ context and sockets
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> subscriber_;     // Market data subscription
    std::unique_ptr<zmq::socket_t> execution_sub_;  // Order execution subscription  
    std::unique_ptr<zmq::socket_t> signal_pub_;     // Trading signal publisher
    
    // Processing control
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> processing_thread_;
    
    // Strategies
    std::vector<std::unique_ptr<Strategy>> strategies_;
    
    // Statistics
    std::atomic<uint64_t> market_data_processed_;
    std::atomic<uint64_t> signals_generated_;
    
    // Main processing loop
    void process_messages();
    
    // Message handlers
    void handle_market_data(const MarketData& data);
    void handle_execution(const OrderExecution& execution);
    
    // Publish trading signal
    void publish_signal(const TradingSignal& signal);
    
    // Performance monitoring
    void log_statistics();
    
    Logger logger_;
};

} // namespace hft