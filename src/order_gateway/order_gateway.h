#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include "alpaca_client.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace hft {

struct Order {
    uint64_t order_id;
    std::string symbol;
    SignalAction action;
    OrderType type;
    double price;
    uint32_t quantity;
    uint32_t filled_quantity;
    std::chrono::steady_clock::time_point created_time;
    std::string external_order_id;  // For broker order ID (Alpaca)
    
    Order() : order_id(0), action(SignalAction::BUY), type(OrderType::MARKET)
        , price(0.0), quantity(0), filled_quantity(0), created_time(std::chrono::steady_clock::now()) {}
    
    Order(uint64_t id, const TradingSignal& signal)
        : order_id(id), symbol(signal.symbol), action(signal.action)
        , type(signal.order_type), price(signal.price), quantity(signal.quantity)
        , filled_quantity(0), created_time(std::chrono::steady_clock::now()) {}
};

class OrderGateway {
public:
    OrderGateway();
    ~OrderGateway();
    
    bool initialize();
    void start();
    void stop();
    bool is_running() const;

private:
    // ZeroMQ sockets
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> signal_subscriber_;
    std::unique_ptr<zmq::socket_t> execution_publisher_;
    
    // Processing control
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> processing_thread_;
    
    // Order management
    std::unordered_map<uint64_t, Order> active_orders_;
    std::atomic<uint64_t> next_order_id_;
    
    // Alpaca integration (optional)
    std::unique_ptr<AlpacaClient> alpaca_client_;
    bool use_alpaca_;
    
    // Statistics
    std::atomic<uint64_t> orders_processed_;
    std::atomic<uint64_t> orders_filled_;
    
    void process_signals();
    void handle_trading_signal(const TradingSignal& signal);
    void simulate_order_fill(const Order& order);
    void handle_alpaca_order(Order& order);
    void publish_execution(const OrderExecution& execution);
    void log_statistics();
    
    Logger logger_;
};

} // namespace hft