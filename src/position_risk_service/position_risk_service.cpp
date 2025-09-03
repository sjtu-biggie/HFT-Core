#include "position_risk_service.h"
#include "../common/static_config.h"
#include "../common/metrics_collector.h"
#include "../common/metrics_publisher.h"
#include "../common/hft_metrics.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace hft {

PositionRiskService::PositionRiskService()
    : running_(false), max_position_value_(100000.0), max_daily_loss_(5000.0)
    , current_daily_pnl_(0.0), positions_updated_(0), risk_checks_(0), risk_violations_(0)
    , logger_("PositionRiskService", StaticConfig::get_logger_endpoint())
    , metrics_publisher_("PositionRiskService", ("tcp://*:" + std::to_string(StaticConfig::get_position_risk_service_metrics_port())).c_str()) {
}

PositionRiskService::~PositionRiskService() {
    stop();
}

bool PositionRiskService::initialize() {
    logger_.info("Initializing Position & Risk Service");
    
    MetricsCollector::instance().initialize();
    StaticConfig::load_from_file("config/hft_config.conf");

    // Initialize metrics publisher
    if (!metrics_publisher_.initialize()) {
        logger_.error("Failed to initialize metrics publisher");
        return false;
    }
    
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        
        // Execution subscriber
        execution_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        execution_subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        execution_subscriber_->connect(StaticConfig::get_executions_endpoint());
        
        // Market data subscriber
        market_data_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        market_data_subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        market_data_subscriber_->connect(StaticConfig::get_market_data_endpoint());
        
        // Position publisher
        position_publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        position_publisher_->bind(StaticConfig::get_positions_endpoint());
        
        logger_.info("Position & Risk Service initialized");
        return true;
        
    } catch (const zmq::error_t& e) {
        logger_.error("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

void PositionRiskService::start() {
    if (running_.load()) {
        logger_.warning("Service already running");
        return;
    }
    
    logger_.info("Starting Position & Risk Service");
    running_.store(true);
    
    // Start metrics publisher
    metrics_publisher_.start();
    
    processing_thread_ = std::make_unique<std::thread>(&PositionRiskService::process_messages, this);
    metrics_thread_ = std::make_unique<std::thread>(&PositionRiskService::metrics_update_loop, this);
    logger_.info("Service started");
}

void PositionRiskService::stop() {
    if (!running_.load()) return;
    
    logger_.info("Stopping service");
    running_.store(false);
    
    // Stop metrics publisher
    metrics_publisher_.stop();
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    if (metrics_thread_ && metrics_thread_->joinable()) {
        metrics_thread_->join();
    }
    
    if (execution_subscriber_) {
        try {
            execution_subscriber_->close();
            execution_subscriber_.reset();
        } catch (const zmq::error_t&) {}
    }
    if (market_data_subscriber_) {
        try {
            market_data_subscriber_->close();
            market_data_subscriber_.reset();
        } catch (const zmq::error_t&) {}
    }
    if (position_publisher_) {
        try {
            position_publisher_->close();
            position_publisher_.reset();
        } catch (const zmq::error_t&) {}
    }
    
    logger_.info("Service stopped");
}

bool PositionRiskService::is_running() const {
    return running_.load();
}

void PositionRiskService::process_messages() {
    logger_.info("Processing thread started");
    
    zmq::pollitem_t items[] = {
        { *execution_subscriber_, 0, ZMQ_POLLIN, 0 },
        { *market_data_subscriber_, 0, ZMQ_POLLIN, 0 }
    };
    
    while (running_.load()) {
        try {
            zmq::poll(&items[0], 2, std::chrono::milliseconds(100));
            
            if (items[0].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                if (execution_subscriber_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(OrderExecution)) {
                        OrderExecution execution;
                        std::memcpy(&execution, message.data(), sizeof(OrderExecution));
                        handle_execution(execution);
                    }
                }
            }
            
            if (items[1].revents & ZMQ_POLLIN) {
                zmq::message_t message;
                if (market_data_subscriber_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(MarketData)) {
                        MarketData data;
                        std::memcpy(&data, message.data(), sizeof(MarketData));
                        handle_market_data(data);
                    }
                }
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EINTR) {
                logger_.error("Processing error: " + std::string(e.what()));
            }
        }
    }
    
    logger_.info("Processing thread stopped");
}

void PositionRiskService::handle_execution(const OrderExecution& execution) {
    HFT_RDTSC_TIMER(hft::metrics::TOTAL_LATENCY);
    
    std::string symbol(execution.symbol);
    auto& position = positions_[symbol];
    
    int32_t qty_change = (execution.exec_type == ExecutionType::FILL) ? 
                        static_cast<int32_t>(execution.fill_quantity) : 0;
    
    // Update position
    if (position.quantity == 0) {
        position.symbol = symbol;
        position.quantity = qty_change;
        position.average_price = execution.fill_price;
    } else {
        // Update average price
        double total_cost = position.quantity * position.average_price + 
                           qty_change * execution.fill_price;
        position.quantity += qty_change;
        if (position.quantity != 0) {
            position.average_price = total_cost / position.quantity;
        }
    }
    
    positions_updated_++;
    HFT_COMPONENT_COUNTER(hft::metrics::POSITIONS_UPDATED_TOTAL);
    
    publish_position_update(symbol);
    logger_.info("Position updated: " + symbol + " qty=" + std::to_string(position.quantity));
}

void PositionRiskService::handle_market_data(const MarketData& data) {
    std::string symbol(data.symbol);
    current_prices_[symbol] = data.last_price;
    update_unrealized_pnl();
}

void PositionRiskService::update_unrealized_pnl() {
    for (auto& [symbol, position] : positions_) {
        if (position.quantity != 0 && current_prices_.count(symbol)) {
            double current_price = current_prices_[symbol];
            position.unrealized_pnl = (current_price - position.average_price) * position.quantity;
        }
    }
}

void PositionRiskService::publish_position_update(const std::string& symbol) {
    if (!positions_.count(symbol)) return;
    
    const auto& position = positions_[symbol];
    
    PositionUpdate update{};
    update.header = MessageFactory::create_header(MessageType::POSITION_UPDATE, 
                                                 sizeof(PositionUpdate) - sizeof(MessageHeader));
    std::strncpy(update.symbol, symbol.c_str(), sizeof(update.symbol) - 1);
    update.position = position.quantity;
    update.average_price = position.average_price;
    update.unrealized_pnl = position.unrealized_pnl;
    update.realized_pnl = position.realized_pnl;
    update.market_value = current_prices_.count(symbol) ? 
                         current_prices_[symbol] * position.quantity : 0.0;
    
    try {
        zmq::message_t message(sizeof(PositionUpdate));
        std::memcpy(message.data(), &update, sizeof(PositionUpdate));
        position_publisher_->send(message, zmq::send_flags::dontwait);
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {
            logger_.error("Failed to publish position update: " + std::string(e.what()));
        }
    }
}

bool PositionRiskService::check_risk_limits(const TradingSignal& signal) {
    HFT_RDTSC_TIMER(hft::metrics::RISK_CHECK_LATENCY);
    
    risk_checks_++;
    HFT_COMPONENT_COUNTER(hft::metrics::RISK_CHECKS_TOTAL);
    
    // Check position size limits
    std::string symbol(signal.symbol);
    if (positions_.count(symbol)) {
        const auto& position = positions_[symbol];
        double proposed_value = std::abs(position.quantity + static_cast<int32_t>(signal.quantity)) * signal.price;
        
        if (proposed_value > max_position_value_) {
            risk_violations_++;
            HFT_COMPONENT_COUNTER(hft::metrics::RISK_VIOLATIONS_TOTAL);
            return false;
        }
    }
    
    // Check daily P&L limits
    if (current_daily_pnl_ < -max_daily_loss_) {
        risk_violations_++;
        HFT_COMPONENT_COUNTER(hft::metrics::RISK_VIOLATIONS_TOTAL);
        return false;
    }
    
    return true;
}

void PositionRiskService::update_metrics() {
    double total_unrealized = 0.0;
    double total_realized = 0.0;
    double gross_exposure = 0.0;
    double net_exposure = 0.0;
    
    for (const auto& [symbol, position] : positions_) {
        total_unrealized += position.unrealized_pnl;
        total_realized += position.realized_pnl;
        
        // Calculate exposure (using current prices if available)
        double market_value = 0.0;
        if (current_prices_.count(symbol)) {
            market_value = position.quantity * current_prices_[symbol];
        } else {
            market_value = position.quantity * position.average_price;
        }
        
        gross_exposure += std::abs(market_value);
        net_exposure += market_value;
    }
    
    // Update metrics
    HFT_GAUGE_VALUE(hft::metrics::POSITIONS_OPEN_COUNT, positions_.size());
    HFT_GAUGE_VALUE(hft::metrics::PNL_UNREALIZED_USD, static_cast<double>(total_unrealized));
    HFT_GAUGE_VALUE(hft::metrics::PNL_REALIZED_USD, static_cast<double>(total_realized));
    HFT_GAUGE_VALUE(hft::metrics::PNL_TOTAL_USD, static_cast<double>(total_unrealized + total_realized));
    HFT_GAUGE_VALUE(hft::metrics::GROSS_EXPOSURE_USD, static_cast<uint64_t>(gross_exposure));
    HFT_GAUGE_VALUE(hft::metrics::NET_EXPOSURE_USD, static_cast<uint64_t>(net_exposure));
    
    // Log each symbol's details
    for (const auto& [symbol, position] : positions_) {
        double current_price = current_prices_.count(symbol) ? current_prices_[symbol] : 0.0;
        logger_.info("Symbol: " + symbol + 
                    " | Current Price: " + std::to_string(current_price) +
                    " | Our Avg Price: " + std::to_string(position.average_price) +
                    " | Our Volume: " + std::to_string(position.quantity) +
                    " | Per-Symbol Profit: " + std::to_string(position.unrealized_pnl));
    }
    
    // Log each metric update
    logger_.info("POSITIONS_OPEN_COUNT: " + std::to_string(positions_.size()));
    logger_.info("PNL_UNREALIZED_USD: " + std::to_string(static_cast<double>(total_unrealized)));
    logger_.info("PNL_REALIZED_USD: " + std::to_string(static_cast<double>(total_realized)));
    logger_.info("PNL_TOTAL_USD: " + std::to_string(static_cast<double>(total_unrealized + total_realized)));
    logger_.info("GROSS_EXPOSURE_USD: " + std::to_string(static_cast<uint64_t>(gross_exposure)));
    logger_.info("NET_EXPOSURE_USD: " + std::to_string(static_cast<uint64_t>(net_exposure)));
}

void PositionRiskService::metrics_update_loop() {
    logger_.info("Metrics update loop started");
    
    while (running_.load()) {
        try {
            update_metrics();
            
            // Update metrics at configurable interval
            std::this_thread::sleep_for(std::chrono::seconds(StaticConfig::get_metrics_update_interval_seconds()));
            
        } catch (const std::exception& e) {
            logger_.error("Metrics update error: " + std::string(e.what()));
        }
    }
    
    logger_.info("Metrics update loop stopped");
}


} // namespace hft