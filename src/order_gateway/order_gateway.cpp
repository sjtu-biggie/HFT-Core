#include "order_gateway.h"
#include <random>
#include <chrono>

namespace hft {

OrderGateway::OrderGateway()
    : running_(false), next_order_id_(1), orders_processed_(0), orders_filled_(0)
    , logger_("OrderGateway") {
}

OrderGateway::~OrderGateway() {
    stop();
}

bool OrderGateway::initialize() {
    logger_.info("Initializing Order Gateway");
    
    config_ = std::make_unique<Config>();
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        
        // Signal subscriber
        signal_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PULL);
        int rcvhwm = 1000;
        signal_subscriber_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        signal_subscriber_->connect("tcp://localhost:5558");
        
        // Execution publisher
        execution_publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        int sndhwm = 1000;
        execution_publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        int linger = 0;
        execution_publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        execution_publisher_->bind("tcp://localhost:5557");
        
        logger_.info("Order Gateway initialized in paper trading mode");
        return true;
        
    } catch (const zmq::error_t& e) {
        logger_.error("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void OrderGateway::start() {
    if (running_.load()) {
        logger_.warning("Order Gateway already running");
        return;
    }
    
    logger_.info("Starting Order Gateway");
    running_.store(true);
    
    processing_thread_ = std::make_unique<std::thread>(&OrderGateway::process_signals, this);
    logger_.info("Order Gateway started");
}

void OrderGateway::stop() {
    if (!running_.load()) {
        return;
    }
    
    logger_.info("Stopping Order Gateway");
    running_.store(false);
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    if (signal_subscriber_) signal_subscriber_->close();
    if (execution_publisher_) execution_publisher_->close();
    
    log_statistics();
    logger_.info("Order Gateway stopped");
}

bool OrderGateway::is_running() const {
    return running_.load();
}

void OrderGateway::process_signals() {
    logger_.info("Signal processing thread started");
    
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(30);
    
    while (running_.load()) {
        try {
            zmq::message_t message;
            if (signal_subscriber_->recv(message, zmq::recv_flags::dontwait)) {
                if (message.size() == sizeof(TradingSignal)) {
                    TradingSignal signal;
                    std::memcpy(&signal, message.data(), sizeof(TradingSignal));
                    handle_trading_signal(signal);
                }
            }
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= stats_interval) {
                log_statistics();
                last_stats_time = now;
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN && e.num() != EINTR) {
                logger_.error("Signal processing error: " + std::string(e.what()));
            }
        }
    }
    
    logger_.info("Signal processing thread stopped");
}

void OrderGateway::handle_trading_signal(const TradingSignal& signal) {
    // Create new order
    uint64_t order_id = next_order_id_++;
    Order order(order_id, signal);
    
    logger_.info("Processing " + std::string(signal.action == SignalAction::BUY ? "BUY" : "SELL") +
                " signal for " + order.symbol + 
                " qty=" + std::to_string(signal.quantity) +
                " price=" + std::to_string(signal.price));
    
    // Store order
    active_orders_[order_id] = order;
    orders_processed_++;
    
    // Simulate order processing in paper trading mode
    if (config_->get_bool(GlobalConfig::PAPER_TRADING, true)) {
        simulate_order_fill(order);
    }
    // TODO: Add real broker integration for live trading
}

void OrderGateway::simulate_order_fill(const Order& order) {
    // Simulate realistic order fill behavior
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Simulate fill delay
    std::uniform_int_distribution<> delay_dist(10, 100); // 10-100ms
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(gen)));
    
    // Simulate fill price with some slippage
    double fill_price = order.price;
    if (order.type == OrderType::MARKET) {
        std::normal_distribution<> slippage_dist(0.0, 0.01); // 1% std dev
        fill_price = fill_price * (1.0 + slippage_dist(gen));
    }
    
    // Create execution report
    OrderExecution execution{};
    execution.header = MessageFactory::create_header(MessageType::ORDER_EXECUTION, 
                                                    sizeof(OrderExecution) - sizeof(MessageHeader));
    execution.order_id = order.order_id;
    std::strncpy(execution.symbol, order.symbol.c_str(), sizeof(execution.symbol) - 1);
    execution.exec_type = ExecutionType::FILL;
    execution.fill_price = fill_price;
    execution.fill_quantity = order.quantity;
    execution.remaining_quantity = 0;
    execution.commission = order.quantity * 0.001; // $0.001 per share
    
    publish_execution(execution);
    
    // Remove from active orders
    active_orders_.erase(order.order_id);
    orders_filled_++;
}

void OrderGateway::publish_execution(const OrderExecution& execution) {
    try {
        zmq::message_t message(sizeof(OrderExecution));
        std::memcpy(message.data(), &execution, sizeof(OrderExecution));
        
        execution_publisher_->send(message, zmq::send_flags::dontwait);
        
        logger_.info("Execution: " + std::string(execution.symbol) +
                    " " + std::to_string(execution.fill_quantity) + 
                    " @ " + std::to_string(execution.fill_price));
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {
            logger_.error("Failed to publish execution: " + std::string(e.what()));
        }
    }
}

void OrderGateway::log_statistics() {
    uint64_t processed = orders_processed_.load();
    uint64_t filled = orders_filled_.load();
    
    std::string stats = "Processed " + std::to_string(processed) +
                       " orders, filled " + std::to_string(filled) +
                       " orders, " + std::to_string(active_orders_.size()) + " active";
    logger_.info(stats);
}

} // namespace hft