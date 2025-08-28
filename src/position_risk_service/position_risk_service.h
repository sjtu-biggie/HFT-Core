#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/config.h"
#include "../common/metrics_publisher.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace hft {

struct Position {
    std::string symbol;
    int32_t quantity;
    double average_price;
    double unrealized_pnl;
    double realized_pnl;
    
    Position() : quantity(0), average_price(0.0), unrealized_pnl(0.0), realized_pnl(0.0) {}
};

class PositionRiskService {
public:
    PositionRiskService();
    ~PositionRiskService();
    
    bool initialize();
    void start();
    void stop();
    bool is_running() const;

private:
    std::unique_ptr<Config> config_;
    
    // ZeroMQ sockets
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> execution_subscriber_;
    std::unique_ptr<zmq::socket_t> market_data_subscriber_;
    std::unique_ptr<zmq::socket_t> position_publisher_;
    
    // Threading
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> processing_thread_;
    
    // Position tracking
    std::unordered_map<std::string, Position> positions_;
    std::unordered_map<std::string, double> current_prices_;
    
    // Risk limits
    double max_position_value_;
    double max_daily_loss_;
    double current_daily_pnl_;
    
    // Statistics
    std::atomic<uint64_t> positions_updated_;
    std::atomic<uint64_t> risk_checks_;
    std::atomic<uint64_t> risk_violations_;
    
    // Metrics
    MetricsPublisher metrics_publisher_;
    
    void process_messages();
    void handle_execution(const OrderExecution& execution);
    void handle_market_data(const MarketData& data);
    void update_unrealized_pnl();
    void publish_position_update(const std::string& symbol);
    bool check_risk_limits(const TradingSignal& signal);
    void log_statistics();
    
    Logger logger_;
};

} // namespace hft