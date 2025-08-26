#include "position_risk_service.h"
#include "../common/static_config.h"
#include <iostream>

namespace hft {

PositionRiskService::PositionRiskService()
    : running_(false), max_position_value_(100000.0), max_daily_loss_(5000.0)
    , current_daily_pnl_(0.0), logger_("PositionRiskService", StaticConfig::get_logger_endpoint()) {
}

PositionRiskService::~PositionRiskService() {
    stop();
}

bool PositionRiskService::initialize() {
    logger_.info("Initializing Position & Risk Service");
    
    config_ = std::make_unique<Config>();
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        
        // Execution subscriber
        execution_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        execution_subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        execution_subscriber_->connect(StaticConfig::get_executions_endpoint());
        
        // Market data subscriber
        market_data_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        market_data_subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        std::string md_endpoint = config_->get_string(GlobalConfig::MARKET_DATA_ENDPOINT);
        market_data_subscriber_->connect(md_endpoint);
        
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
    processing_thread_ = std::make_unique<std::thread>(&PositionRiskService::process_messages, this);
    logger_.info("Service started");
}

void PositionRiskService::stop() {
    if (!running_.load()) return;
    
    logger_.info("Stopping service");
    running_.store(false);
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    if (execution_subscriber_) execution_subscriber_->close();
    if (market_data_subscriber_) market_data_subscriber_->close();
    if (position_publisher_) position_publisher_->close();
    
    log_statistics();
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
    // Implement risk checks here
    return true; // Simplified for now
}

void PositionRiskService::log_statistics() {
    double total_unrealized = 0.0;
    for (const auto& [symbol, position] : positions_) {
        total_unrealized += position.unrealized_pnl;
    }
    
    logger_.info("Positions: " + std::to_string(positions_.size()) + 
                ", Total Unrealized P&L: " + std::to_string(total_unrealized));
}

} // namespace hft