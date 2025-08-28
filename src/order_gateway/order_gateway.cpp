#include "order_gateway.h"
#include "../common/static_config.h"
#include "../common/hft_metrics.h"
#include "../common/metrics_publisher.h"
#include <random>
#include <chrono>

namespace hft {

OrderGateway::OrderGateway()
    : running_(false), next_order_id_(1), use_alpaca_(false), orders_processed_(0), orders_filled_(0), orders_rejected_(0)
    , logger_("OrderGateway", StaticConfig::get_logger_endpoint())
    , metrics_publisher_("OrderGateway", "tcp://*:5563") {
}

OrderGateway::~OrderGateway() {
    stop();
}

bool OrderGateway::initialize() {
    logger_.info("Initializing Order Gateway");
    
    // Initialize metrics publisher
    if (!metrics_publisher_.initialize()) {
        logger_.error("Failed to initialize metrics publisher");
        return false;
    }
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        
        // Signal subscriber
        signal_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        signal_subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);  // Subscribe to all messages
        int rcvhwm = 1000;
        signal_subscriber_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        signal_subscriber_->connect(StaticConfig::get_signals_endpoint());
        
        // Execution publisher
        execution_publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        int sndhwm = 1000;
        execution_publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        int linger = 0;
        execution_publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        execution_publisher_->bind(StaticConfig::get_executions_endpoint());
        
        // Initialize Alpaca client if trading is enabled and not in paper mode
        if (StaticConfig::get_trading_enabled() && !StaticConfig::get_paper_trading()) {
            alpaca_client_ = std::make_unique<AlpacaClient>();
            
            // Try to load Alpaca credentials from environment variables
            const char* api_key = std::getenv("ALPACA_API_KEY");
            const char* api_secret = std::getenv("ALPACA_API_SECRET");
            const char* base_url = std::getenv("ALPACA_BASE_URL");
            
            if (api_key && api_secret) {
                std::string url = base_url ? base_url : "https://paper-api.alpaca.markets";
                if (alpaca_client_->initialize(api_key, api_secret, url)) {
                    use_alpaca_ = true;
                    logger_.info("Alpaca client initialized successfully");
                } else {
                    logger_.warning("Failed to initialize Alpaca client, falling back to paper trading");
                    use_alpaca_ = false;
                }
            } else {
                logger_.warning("Alpaca credentials not found, using paper trading mode");
                use_alpaca_ = false;
            }
        }
        
        std::string mode = use_alpaca_ ? "live trading (Alpaca)" : "paper trading";
        logger_.info("Order Gateway initialized in " + mode + " mode");
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
    
    // Start metrics publisher
    metrics_publisher_.start();
    
    processing_thread_ = std::make_unique<std::thread>(&OrderGateway::process_signals, this);
    logger_.info("Order Gateway started");
}

void OrderGateway::stop() {
    if (!running_.load()) {
        return;
    }
    
    logger_.info("Stopping Order Gateway");
    running_.store(false);
    
    // Stop metrics publisher
    metrics_publisher_.stop();
    
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
    HFT_RDTSC_TIMER(hft::metrics::TOTAL_LATENCY);
    
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
    
    // Record metrics
    HFT_COMPONENT_COUNTER(hft::metrics::ORDERS_RECEIVED_TOTAL);
    
    // Route to appropriate execution method
    if (use_alpaca_) {
        handle_alpaca_order(active_orders_[order_id]);
    } else {
        simulate_order_fill(order);
    }
}

void OrderGateway::simulate_order_fill(const Order& order) {
    HFT_RDTSC_TIMER(hft::metrics::SUBMIT_LATENCY);
    
    // Simulate realistic order fill behavior
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Validate order first
    {
        HFT_RDTSC_TIMER(hft::metrics::VALIDATE_LATENCY);
        if (order.quantity == 0 || order.symbol.empty()) {
            orders_rejected_++;
            HFT_COMPONENT_COUNTER(hft::metrics::ORDERS_REJECTED_TOTAL);
            return;
        }
    }
    
    // Risk check simulation
    {
        HFT_RDTSC_TIMER(hft::metrics::RISK_CHECK_LATENCY);
        // Simulate risk check processing
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    
    // Simulate fill delay
    std::uniform_int_distribution<> delay_dist(10, 100); // 10-100ms
    auto fill_start = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_dist(gen)));
    
    // Record fill latency
    auto fill_end = std::chrono::steady_clock::now();
    auto fill_latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(fill_end - fill_start).count();
    HFT_LATENCY_NS(hft::metrics::FILL_LATENCY, fill_latency_ns);
    
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
    
    // Record metrics
    HFT_COMPONENT_COUNTER(hft::metrics::ORDERS_FILLED_TOTAL);
    HFT_COMPONENT_COUNTER(hft::metrics::ORDERS_SUBMITTED_TOTAL);
    
    // Calculate and record fill rate
    double fill_rate = (orders_filled_.load() * 100.0) / std::max(orders_processed_.load(), 1UL);
    HFT_GAUGE_VALUE(hft::metrics::FILL_RATE_PERCENT, static_cast<uint64_t>(fill_rate));
}

void OrderGateway::handle_alpaca_order(Order& order) {
    if (!alpaca_client_ || !alpaca_client_->is_connected()) {
        logger_.error("Alpaca client not available, falling back to paper trading");
        simulate_order_fill(order);
        return;
    }
    
    try {
        AlpacaOrderResponse response;
        std::string side = (order.action == SignalAction::BUY) ? "buy" : "sell";
        
        // Submit order to Alpaca
        if (order.type == OrderType::MARKET) {
            response = alpaca_client_->submit_market_order(order.symbol, side, order.quantity);
        } else if (order.type == OrderType::LIMIT) {
            response = alpaca_client_->submit_limit_order(order.symbol, side, order.quantity, order.price);
        } else {
            logger_.error("Unsupported order type for Alpaca: " + std::to_string(static_cast<int>(order.type)));
            simulate_order_fill(order);
            return;
        }
        
        if (!response.is_success()) {
            logger_.error("Alpaca order failed: " + response.error_message + ", falling back to paper trading");
            simulate_order_fill(order);
            return;
        }
        
        // Store external order ID
        order.external_order_id = response.order_id;
        active_orders_[order.order_id] = order;
        
        logger_.info("Alpaca order submitted: " + response.order_id);
        
        // Check if order was immediately filled
        if (response.is_filled()) {
            // Create execution report
            OrderExecution execution{};
            execution.header = MessageFactory::create_header(MessageType::ORDER_EXECUTION, 
                                                            sizeof(OrderExecution) - sizeof(MessageHeader));
            execution.order_id = order.order_id;
            std::strncpy(execution.symbol, order.symbol.c_str(), sizeof(execution.symbol) - 1);
            execution.exec_type = ExecutionType::FILL;
            execution.fill_price = response.fill_price;
            execution.fill_quantity = static_cast<uint32_t>(response.filled_qty);
            execution.remaining_quantity = static_cast<uint32_t>(response.quantity - response.filled_qty);
            execution.commission = response.filled_qty * 0.001; // Estimate commission
            
            publish_execution(execution);
            
            // Remove from active orders if fully filled
            if (execution.remaining_quantity == 0) {
                active_orders_.erase(order.order_id);
                orders_filled_++;
            }
        }
        
    } catch (const std::exception& e) {
        logger_.error("Exception in Alpaca order handling: " + std::string(e.what()) + ", falling back to paper trading");
        simulate_order_fill(order);
    }
}

void OrderGateway::publish_execution(const OrderExecution& execution) {
    HFT_RDTSC_TIMER(hft::metrics::PUBLISH_LATENCY);
    
    try {
        zmq::message_t message(sizeof(OrderExecution));
        std::memcpy(message.data(), &execution, sizeof(OrderExecution));
        
        execution_publisher_->send(message, zmq::send_flags::dontwait);
        
        logger_.info("Execution: " + std::string(execution.symbol) +
                    " " + std::to_string(execution.fill_quantity) + 
                    " @ " + std::to_string(execution.fill_price));
        
        // Update throughput metrics
        static auto last_rate_update = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_rate_update).count();
        if (elapsed >= 1) {
            uint64_t orders_per_sec = orders_filled_.load() / std::max(elapsed, 1L);
            HFT_GAUGE_VALUE(hft::metrics::ORDERS_PER_SECOND, orders_per_sec);
            last_rate_update = now;
        }
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {
            logger_.error("Failed to publish execution: " + std::string(e.what()));
        }
    }
}

void OrderGateway::log_statistics() {
    uint64_t processed = orders_processed_.load();
    uint64_t filled = orders_filled_.load();
    uint64_t rejected = orders_rejected_.load();
    
    std::string stats = "Processed " + std::to_string(processed) +
                       " orders, filled " + std::to_string(filled) +
                       " orders, rejected " + std::to_string(rejected) +
                       " orders, " + std::to_string(active_orders_.size()) + " active";
    logger_.info(stats);
    
    // Update gauge metrics
    HFT_GAUGE_VALUE(hft::metrics::POSITIONS_OPEN_COUNT, active_orders_.size());
    
    // Update counters that might have been missed
    // (Note: In a real system, these would be updated atomically at the time of the event)
    for (uint64_t i = 0; i < processed; ++i) {
        // This is a simplified approach for demo - in production you'd update these individually
    }
}

} // namespace hft