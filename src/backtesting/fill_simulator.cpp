#include "fill_simulator.h"
#include "../common/static_config.h"
#include <random>
#include <algorithm>
#include <sstream>
#include <cstring>

namespace hft {

FillSimulator::FillSimulator()
    : realistic_spreads_(true), total_fills_(0), partial_fills_(0)
    , total_slippage_(0.0), total_commission_(0.0)
    , logger_("FillSimulator", StaticConfig::get_logger_endpoint()) {
}

FillSimulator::~FillSimulator() = default;

bool FillSimulator::initialize(const FillConfig& config) {
    config_ = config;
    logger_.info("Initializing Fill Simulator with model: " + 
                std::to_string(static_cast<int>(config.model)));
    
    // Initialize default volatilities for common symbols
    for (const char* symbol : StaticConfig::DEFAULT_SYMBOLS) {
        if (symbol) {
            symbol_volatilities_[symbol] = 0.02; // 2% daily volatility
        }
    }
    
    return true;
}

void FillSimulator::update_market_state(const MarketData& market_data) {
    std::string symbol = market_data.symbol;
    
    MarketState& state = market_states_[symbol];
    double old_price = state.last_price;
    
    state.symbol = symbol;
    state.bid_price = market_data.bid_price;
    state.ask_price = market_data.ask_price;
    state.last_price = market_data.last_price;
    state.bid_volume = market_data.bid_size;
    state.ask_volume = market_data.ask_size;
    state.spread = market_data.ask_price - market_data.bid_price;
    state.timestamp = market_data.header.timestamp;
    
    // Update volatility based on price changes
    if (old_price > 0.0) {
        double price_change = std::abs(state.last_price - old_price) / old_price;
        update_volatility(symbol, price_change);
    }
    
    // Generate realistic spread if enabled
    if (realistic_spreads_) {
        state.spread = generate_realistic_spread(symbol, state.last_price);
        state.bid_price = state.last_price - state.spread / 2.0;
        state.ask_price = state.last_price + state.spread / 2.0;
    }
    
    state.volatility = symbol_volatilities_[symbol];
    
    // Process any pending orders for this symbol
    for (auto& [order_id, order] : pending_orders_) {
        if (order.symbol == symbol) {
            process_order_fill(order);
        }
    }
}

void FillSimulator::submit_order(uint64_t order_id, const std::string& symbol, 
                                SignalAction action, OrderType type, 
                                double price, uint32_t quantity) {
    PendingOrder order{};
    order.order_id = order_id;
    order.symbol = symbol;
    order.action = action;
    order.type = type;
    order.price = price;
    order.quantity = quantity;
    order.filled_quantity = 0;
    order.submit_time = current_time();
    order.last_update = order.submit_time;
    
    pending_orders_[order_id] = order;
    
    logger_.info("Order submitted: " + std::to_string(order_id) + 
                " " + symbol + " " + 
                (action == SignalAction::BUY ? "BUY" : "SELL") + 
                " " + std::to_string(quantity) + "@" + std::to_string(price));
    
    // Process immediately for some fill models
    if (config_.model == FillModel::IMMEDIATE) {
        process_order_fill(pending_orders_[order_id]);
    }
}

void FillSimulator::cancel_order(uint64_t order_id) {
    auto it = pending_orders_.find(order_id);
    if (it != pending_orders_.end()) {
        logger_.info("Order canceled: " + std::to_string(order_id));
        pending_orders_.erase(it);
    }
}

void FillSimulator::process_pending_fills() {
    timestamp_t now = current_time();
    
    // Process queued fill events
    while (!fill_queue_.empty() && fill_queue_.top().fill_time <= now) {
        const FillEvent& event = fill_queue_.top();
        
        auto it = pending_orders_.find(event.order_id);
        if (it != pending_orders_.end()) {
            PendingOrder& order = it->second;
            
            // Create execution report
            OrderExecution execution{};
            execution.header = MessageFactory::create_header(MessageType::ORDER_EXECUTION, 
                                                            sizeof(OrderExecution) - sizeof(MessageHeader));
            execution.order_id = event.order_id;
            std::strncpy(execution.symbol, order.symbol.c_str(), sizeof(execution.symbol) - 1);
            execution.exec_type = event.exec_type;
            execution.fill_price = event.fill_price;
            execution.fill_quantity = event.fill_quantity;
            execution.remaining_quantity = order.quantity - order.filled_quantity - event.fill_quantity;
            execution.commission = calculate_commission(event.fill_price, event.fill_quantity);
            
            // Update order state
            order.filled_quantity += event.fill_quantity;
            order.last_update = now;
            
            // Track statistics
            total_fills_++;
            total_commission_ += execution.commission;
            
            if (event.exec_type == ExecutionType::PARTIAL_FILL) {
                partial_fills_++;
            }
            
            // Calculate and track slippage
            double expected_price = (order.action == SignalAction::BUY) ? 
                order.price : order.price;
            double slippage = std::abs(event.fill_price - expected_price) / expected_price;
            total_slippage_ += slippage;
            
            // Send fill notification
            if (fill_callback_) {
                fill_callback_(execution);
            }
            
            // Remove order if fully filled
            if (order.filled_quantity >= order.quantity) {
                pending_orders_.erase(it);
            }
        }
        
        fill_queue_.pop();
    }
    
    // Process pending orders that need market data
    for (auto& [order_id, order] : pending_orders_) {
        if (market_states_.find(order.symbol) != market_states_.end()) {
            process_order_fill(order);
        }
    }
}

void FillSimulator::process_order_fill(PendingOrder& order) {
    auto market_it = market_states_.find(order.symbol);
    if (market_it == market_states_.end()) {
        return; // No market data available
    }
    
    const MarketState& market = market_it->second;
    
    // Skip if market is closed and we respect market hours
    if (config_.respect_market_hours && !is_market_open(current_time())) {
        return;
    }
    
    // Check if order can be filled based on price
    bool can_fill = false;
    if (order.type == OrderType::MARKET) {
        can_fill = true;
    } else if (order.type == OrderType::LIMIT) {
        if (order.action == SignalAction::BUY && order.price >= market.ask_price) {
            can_fill = true;
        } else if (order.action == SignalAction::SELL && order.price <= market.bid_price) {
            can_fill = true;
        }
    }
    
    if (!can_fill) {
        return;
    }
    
    // Calculate fill event
    FillEvent event = calculate_fill_event(order, market);
    
    if (event.fill_quantity > 0) {
        fill_queue_.push(event);
        
        logger_.info("Fill scheduled: " + std::to_string(order.order_id) + 
                    " " + std::to_string(event.fill_quantity) + 
                    "@" + std::to_string(event.fill_price) + 
                    " at " + std::to_string(event.fill_time.count()));
    }
}

FillEvent FillSimulator::calculate_fill_event(const PendingOrder& order, const MarketState& market) {
    FillEvent event{};
    event.order_id = order.order_id;
    event.fill_time = current_time() + std::chrono::milliseconds(calculate_latency(order));
    
    // Calculate fill price and quantity
    event.fill_price = calculate_fill_price(order, market);
    event.fill_quantity = calculate_fill_quantity(order, market);
    
    // Determine execution type
    uint32_t remaining = order.quantity - order.filled_quantity;
    if (event.fill_quantity >= remaining) {
        event.exec_type = ExecutionType::FILL;
        event.fill_quantity = remaining;
    } else {
        event.exec_type = ExecutionType::PARTIAL_FILL;
    }
    
    return event;
}

double FillSimulator::calculate_fill_price(const PendingOrder& order, const MarketState& market) {
    double base_price;
    
    // Start with market price
    if (order.type == OrderType::MARKET) {
        base_price = (order.action == SignalAction::BUY) ? market.ask_price : market.bid_price;
    } else {
        base_price = order.price;
    }
    
    // Apply slippage based on model
    double slippage = 0.0;
    switch (config_.model) {
        case FillModel::IMMEDIATE:
            // No slippage for immediate fills
            break;
            
        case FillModel::REALISTIC_SLIPPAGE:
            slippage = calculate_slippage(order, market);
            break;
            
        case FillModel::MARKET_IMPACT:
            slippage = calculate_market_impact(order, market);
            break;
            
        case FillModel::LATENCY_AWARE:
        case FillModel::PARTIAL_FILLS:
            slippage = calculate_slippage(order, market) + calculate_market_impact(order, market);
            break;
    }
    
    // Apply slippage
    if (order.action == SignalAction::BUY) {
        return base_price * (1.0 + slippage);
    } else {
        return base_price * (1.0 - slippage);
    }
}

uint32_t FillSimulator::calculate_fill_quantity(const PendingOrder& order, const MarketState& market) {
    uint32_t remaining = order.quantity - order.filled_quantity;
    
    // For immediate and simple models, fill completely
    if (config_.model == FillModel::IMMEDIATE || config_.model == FillModel::REALISTIC_SLIPPAGE) {
        return remaining;
    }
    
    // For partial fills model, sometimes do partial fills
    if (config_.model == FillModel::PARTIAL_FILLS) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        
        if (dis(gen) < config_.partial_fill_probability) {
            // Partial fill: 20-80% of remaining quantity
            std::uniform_real_distribution<> fill_ratio(0.2, 0.8);
            return static_cast<uint32_t>(remaining * fill_ratio(gen));
        }
    }
    
    // Consider available liquidity
    uint64_t available_liquidity = (order.action == SignalAction::BUY) ? 
        market.ask_volume : market.bid_volume;
    
    if (available_liquidity > 0 && remaining > available_liquidity) {
        return static_cast<uint32_t>(std::min(static_cast<uint64_t>(remaining), available_liquidity));
    }
    
    return remaining;
}

double FillSimulator::calculate_slippage(const PendingOrder& order, const MarketState& market) {
    double base_slippage = config_.slippage_factor;
    
    // Increase slippage based on volatility
    base_slippage *= (1.0 + market.volatility * config_.volatility_impact);
    
    // Increase slippage for large orders relative to spread
    double spread_impact = market.spread_bps() / 10000.0; // Convert bps to decimal
    base_slippage += spread_impact * 0.5;
    
    // Add randomness
    return base_slippage * random_uniform(0.5, 1.5);
}

double FillSimulator::calculate_market_impact(const PendingOrder& order, const MarketState& market) {
    // Market impact based on order size relative to average volume
    uint64_t avg_volume = (market.bid_volume + market.ask_volume) / 2;
    if (avg_volume == 0) avg_volume = 1000; // Default
    
    double size_ratio = static_cast<double>(order.quantity) / avg_volume;
    return config_.market_impact_factor * size_ratio;
}

int FillSimulator::calculate_latency(const PendingOrder& order) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(config_.min_latency_ms, config_.max_latency_ms);
    return dis(gen);
}

double FillSimulator::calculate_commission(double fill_price, uint32_t fill_quantity) {
    double commission = 0.0;
    
    // Per-share commission
    commission += config_.commission_per_share * fill_quantity;
    
    // Percentage commission
    commission += (fill_price * fill_quantity) * config_.commission_percentage;
    
    // Apply minimum commission
    commission = std::max(commission, config_.minimum_commission);
    
    return commission;
}

bool FillSimulator::is_market_open(timestamp_t timestamp) {
    // Simplified market hours check (9:30 AM - 4:00 PM ET)
    // In a real implementation, this would consider holidays, timezone, etc.
    auto time_t_val = std::chrono::duration_cast<std::chrono::seconds>(timestamp).count();
    
    std::tm* tm = std::gmtime(&time_t_val);
    int hour = tm->tm_hour;
    int minute = tm->tm_min;
    int total_minutes = hour * 60 + minute;
    
    // Convert to ET (simplified - not handling DST)
    total_minutes -= 5 * 60; // UTC to ET
    if (total_minutes < 0) total_minutes += 24 * 60;
    
    int market_open = 9 * 60 + 30;  // 9:30 AM
    int market_close = 16 * 60;     // 4:00 PM
    
    return total_minutes >= market_open && total_minutes <= market_close;
}

double FillSimulator::generate_realistic_spread(const std::string& symbol, double price) {
    // Generate spreads based on typical values for different price ranges
    double spread_bps;
    
    if (price < 5.0) {
        spread_bps = 20.0; // 20 bps for penny stocks
    } else if (price < 50.0) {
        spread_bps = 5.0;  // 5 bps for mid-price stocks
    } else if (price < 200.0) {
        spread_bps = 2.0;  // 2 bps for higher price stocks
    } else {
        spread_bps = 1.0;  // 1 bp for very high price stocks
    }
    
    // Add randomness
    spread_bps *= random_uniform(0.5, 2.0);
    
    return price * spread_bps / 10000.0;
}

void FillSimulator::update_volatility(const std::string& symbol, double price_change) {
    // Simple exponential moving average of volatility
    double& vol = symbol_volatilities_[symbol];
    double alpha = 0.1; // Decay factor
    vol = alpha * price_change + (1.0 - alpha) * vol;
}

void FillSimulator::set_volatility_model(const std::string& symbol, double volatility) {
    symbol_volatilities_[symbol] = volatility;
    logger_.info("Set volatility for " + symbol + ": " + std::to_string(volatility));
}

double FillSimulator::get_average_slippage() const {
    uint64_t fills = total_fills_.load();
    return fills > 0 ? total_slippage_ / fills : 0.0;
}

timestamp_t FillSimulator::current_time() {
    return std::chrono::duration_cast<timestamp_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch());
}

double FillSimulator::random_normal(double mean, double stddev) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::normal_distribution<> dis(mean, stddev);
    return dis(gen);
}

double FillSimulator::random_uniform(double min, double max) {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    return dis(gen);
}

} // namespace hft