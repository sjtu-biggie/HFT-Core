#include "enhanced_strategies.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace hft {

// MarketMakingStrategy Implementation
MarketMakingStrategy::MarketMakingStrategy(uint64_t strategy_id)
    : OrderBookStrategy(strategy_id, "MarketMaking") {
}

bool MarketMakingStrategy::initialize() {
    logger_.info("Initializing Market Making Strategy with ID: " + std::to_string(strategy_id_));
    
    // Initialize with default parameters - could be loaded from config
    params_.spread_threshold = 0.001;
    params_.quote_size_ratio = 0.1;
    params_.max_inventory = 1000.0;
    params_.inventory_skew_factor = 0.5;
    params_.min_quote_size = 100;
    params_.max_quote_size = 500;
    
    return true;
}

void MarketMakingStrategy::on_market_data(const MarketData& data) {
    std::string symbol(data.symbol);
    
    // Update position tracking if needed (simplified)
    if (positions_.find(symbol) == positions_.end()) {
        positions_[symbol] = 0.0;
    }
    
    // Evaluate market making opportunity
    evaluate_market_making_opportunity(symbol);
}

void MarketMakingStrategy::on_order_book_update(const OrderBookUpdate& update) {
    std::string symbol(update.symbol);
    
    // Process order book update
    book_manager_.process_update(update);
    
    // Re-evaluate quotes if book structure changed significantly
    const auto* book = book_manager_.get_book(symbol);
    if (book && should_quote(symbol, book)) {
        generate_quotes(symbol, book);
    }
}

void MarketMakingStrategy::on_execution(const OrderExecution& execution) {
    std::string symbol(execution.symbol);
    
    // Update position tracking
    if (execution.exec_type == ExecutionType::FILL || 
        execution.exec_type == ExecutionType::PARTIAL_FILL) {
        
        // Update position (simplified - need to track order direction)
        positions_[symbol] += execution.fill_quantity;  // Assuming all fills are buys for now
        
        logger_.info("Position updated for " + symbol + ": " + 
                    std::to_string(positions_[symbol]));
    }
}

void MarketMakingStrategy::evaluate_market_making_opportunity(const std::string& symbol) {
    const auto* book = book_manager_.get_book(symbol);
    if (!book || !book->is_valid()) return;
    
    if (should_quote(symbol, book)) {
        generate_quotes(symbol, book);
    }
}

void MarketMakingStrategy::generate_quotes(const std::string& symbol, const OrderBook* book) {
    auto now = std::chrono::steady_clock::now();
    
    // Rate limiting
    auto it = last_quote_time_.find(symbol);
    if (it != last_quote_time_.end() && (now - it->second) < MIN_QUOTE_INTERVAL) {
        return;
    }
    
    double fair_value = calculate_fair_value(book);
    double skew = calculate_quote_skew(symbol);
    double spread = book->get_spread();
    
    // Calculate quote prices with inventory skew
    double bid_price = fair_value - (spread / 4.0) - skew;
    double ask_price = fair_value + (spread / 4.0) - skew;  // Negative skew moves ask down when long
    
    // Calculate quote sizes based on best level
    uint32_t best_bid_size = book->get_bid_size_at_level(0);
    uint32_t best_ask_size = book->get_ask_size_at_level(0);
    
    uint32_t bid_size = std::max(params_.min_quote_size,
                                std::min(params_.max_quote_size,
                                        static_cast<uint32_t>(best_bid_size * params_.quote_size_ratio)));
    uint32_t ask_size = std::max(params_.min_quote_size,
                                std::min(params_.max_quote_size,
                                        static_cast<uint32_t>(best_ask_size * params_.quote_size_ratio)));
    
    // Generate bid signal
    TradingSignal bid_signal = MessageFactory::create_trading_signal(
        symbol, SignalAction::BUY, OrderType::LIMIT, bid_price, bid_size, strategy_id_, 0.8
    );
    
    // Generate ask signal  
    TradingSignal ask_signal = MessageFactory::create_trading_signal(
        symbol, SignalAction::SELL, OrderType::LIMIT, ask_price, ask_size, strategy_id_, 0.8
    );
    
    logger_.info("Generated MM quotes for " + symbol + 
                ": BID " + std::to_string(bid_size) + "@" + std::to_string(bid_price) +
                ", ASK " + std::to_string(ask_size) + "@" + std::to_string(ask_price));
    
    last_quote_time_[symbol] = now;
}

double MarketMakingStrategy::calculate_fair_value(const OrderBook* book) const {
    // Use mid-price as fair value (could be enhanced with VWAP, etc.)
    return book->get_mid_price();
}

double MarketMakingStrategy::calculate_quote_skew(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    if (it == positions_.end()) return 0.0;
    
    double position = it->second;
    double max_pos = params_.max_inventory;
    
    // Skew quotes away from inventory
    // If long (positive position), skew quotes down to encourage selling
    return -(position / max_pos) * params_.inventory_skew_factor;
}

bool MarketMakingStrategy::should_quote(const std::string& symbol, const OrderBook* book) const {
    // Check minimum spread requirement
    double spread = book->get_spread();
    double mid_price = book->get_mid_price();
    
    if (mid_price <= 0.0 || spread <= 0.0) return false;
    
    double relative_spread = spread / mid_price;
    if (relative_spread < params_.spread_threshold) {
        return false;
    }
    
    // Check inventory limits
    auto pos_it = positions_.find(symbol);
    if (pos_it != positions_.end()) {
        if (std::abs(pos_it->second) >= params_.max_inventory) {
            return false;
        }
    }
    
    return true;
}

// StatArbStrategy Implementation (simplified)
StatArbStrategy::StatArbStrategy(uint64_t strategy_id)
    : OrderBookStrategy(strategy_id, "StatArb") {
}

bool StatArbStrategy::initialize() {
    logger_.info("Initializing Statistical Arbitrage Strategy with ID: " + std::to_string(strategy_id_));
    return true;
}

void StatArbStrategy::on_market_data(const MarketData& data) {
    // Placeholder - basic market data handling
}

void StatArbStrategy::on_order_book_update(const OrderBookUpdate& update) {
    std::string symbol(update.symbol);
    book_manager_.process_update(update);
    
    const auto* book = book_manager_.get_book(symbol);
    if (book && book->is_valid()) {
        update_market_state(symbol, book);
        evaluate_stat_arb_signal(symbol);
    }
}

void StatArbStrategy::on_execution(const OrderExecution& execution) {
    std::string symbol(execution.symbol);
    logger_.info("StatArb execution for " + symbol + ": " + 
                std::to_string(execution.fill_quantity) + " @ " +
                std::to_string(execution.fill_price));
}

void StatArbStrategy::update_market_state(const std::string& symbol, const OrderBook* book) {
    auto& state = market_states_[symbol];
    
    double mid_price = book->get_mid_price();
    double imbalance = book->get_bid_ask_imbalance();
    
    // Maintain rolling window
    state.mid_prices.push_back(mid_price);
    state.imbalances.push_back(imbalance);
    
    if (state.mid_prices.size() > params_.lookback_periods) {
        state.mid_prices.erase(state.mid_prices.begin());
        state.imbalances.erase(state.imbalances.begin());
    }
    
    // Update means
    if (!state.mid_prices.empty()) {
        state.mean_price = std::accumulate(state.mid_prices.begin(), 
                                         state.mid_prices.end(), 0.0) / state.mid_prices.size();
        state.mean_imbalance = std::accumulate(state.imbalances.begin(),
                                             state.imbalances.end(), 0.0) / state.imbalances.size();
    }
}

void StatArbStrategy::evaluate_stat_arb_signal(const std::string& symbol) {
    if (!should_generate_signal(symbol)) return;
    
    const auto& state = market_states_[symbol];
    if (state.mid_prices.size() < params_.lookback_periods) return;
    
    double current_price = state.mid_prices.back();
    double current_imbalance = state.imbalances.back();
    
    double price_z = calculate_z_score(state.mid_prices, current_price);
    double imbalance_z = calculate_z_score(state.imbalances, current_imbalance);
    
    // Generate signal based on mean reversion and imbalance
    if (std::abs(price_z) > params_.price_threshold && 
        std::abs(imbalance_z) > params_.imbalance_threshold) {
        
        SignalAction action = (price_z > 0) ? SignalAction::SELL : SignalAction::BUY;
        
        TradingSignal signal = MessageFactory::create_trading_signal(
            symbol, action, OrderType::MARKET, 0.0, params_.signal_size, 
            strategy_id_, std::min(std::abs(price_z) + std::abs(imbalance_z), 1.0)
        );
        
        logger_.info("Generated StatArb " + std::string(action == SignalAction::BUY ? "BUY" : "SELL") +
                    " signal for " + symbol + " (price_z=" + std::to_string(price_z) + 
                    ", imb_z=" + std::to_string(imbalance_z) + ")");
        
        last_signal_time_[symbol] = std::chrono::steady_clock::now();
    }
}

double StatArbStrategy::calculate_z_score(const std::vector<double>& data, double current_value) const {
    if (data.size() < 2) return 0.0;
    
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    
    double sq_sum = std::inner_product(data.begin(), data.end(), data.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / data.size() - mean * mean);
    
    return (stdev > 0.0) ? (current_value - mean) / stdev : 0.0;
}

bool StatArbStrategy::should_generate_signal(const std::string& symbol) const {
    auto it = last_signal_time_.find(symbol);
    if (it == last_signal_time_.end()) return true;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
    
    return elapsed.count() >= static_cast<int>(params_.min_signal_interval_ms);
}

// Enhanced Momentum Strategy (placeholder implementation)
EnhancedMomentumStrategy::EnhancedMomentumStrategy(uint64_t strategy_id)
    : OrderBookStrategy(strategy_id, "EnhancedMomentum") {
}

bool EnhancedMomentumStrategy::initialize() {
    logger_.info("Initializing Enhanced Momentum Strategy with ID: " + std::to_string(strategy_id_));
    return true;
}

void EnhancedMomentumStrategy::on_market_data(const MarketData& data) {
    // Basic implementation - could be enhanced
}

void EnhancedMomentumStrategy::on_order_book_update(const OrderBookUpdate& update) {
    std::string symbol(update.symbol);
    book_manager_.process_update(update);
    
    const auto* book = book_manager_.get_book(symbol);
    if (book && book->is_valid()) {
        double mid_price = book->get_mid_price();
        double imbalance = book->get_bid_ask_imbalance();
        
        update_momentum_state(symbol, mid_price, imbalance);
        evaluate_momentum_signal(symbol);
    }
}

void EnhancedMomentumStrategy::on_execution(const OrderExecution& execution) {
    // Handle executions
}

void EnhancedMomentumStrategy::update_momentum_state(const std::string& symbol, 
                                                   double mid_price, double imbalance) {
    auto& state = momentum_states_[symbol];
    auto now = std::chrono::steady_clock::now();
    
    if (state.last_mid_price > 0.0) {
        double price_change = (mid_price - state.last_mid_price) / state.last_mid_price;
        state.price_changes.push_back(price_change);
        
        if (state.price_changes.size() > params_.momentum_window) {
            state.price_changes.erase(state.price_changes.begin());
        }
    }
    
    state.flow_imbalances.push_back(imbalance);
    if (state.flow_imbalances.size() > params_.momentum_window) {
        state.flow_imbalances.erase(state.flow_imbalances.begin());
    }
    
    state.last_mid_price = mid_price;
    state.last_update = now;
}

void EnhancedMomentumStrategy::evaluate_momentum_signal(const std::string& symbol) {
    const auto& state = momentum_states_.find(symbol);
    if (state == momentum_states_.end()) return;
    
    if (state->second.price_changes.size() < params_.momentum_window) return;
    
    // Calculate momentum and flow scores
    double momentum_score = calculate_momentum_score(state->second.price_changes);
    double avg_flow = std::accumulate(state->second.flow_imbalances.begin(),
                                    state->second.flow_imbalances.end(), 0.0) / 
                     state->second.flow_imbalances.size();
    
    // Generate signal if thresholds are met
    if (std::abs(momentum_score) > params_.momentum_threshold &&
        std::abs(avg_flow) > params_.flow_threshold) {
        
        double confidence = calculate_signal_confidence(momentum_score, avg_flow);
        uint32_t signal_size = calculate_signal_size(confidence);
        
        SignalAction action = (momentum_score > 0) ? SignalAction::BUY : SignalAction::SELL;
        
        TradingSignal signal = MessageFactory::create_trading_signal(
            symbol, action, OrderType::MARKET, 0.0, signal_size, strategy_id_, confidence
        );
        
        logger_.info("Generated Enhanced Momentum " + 
                    std::string(action == SignalAction::BUY ? "BUY" : "SELL") +
                    " signal for " + symbol + " (momentum=" + std::to_string(momentum_score) + 
                    ", flow=" + std::to_string(avg_flow) + ", size=" + std::to_string(signal_size) + ")");
        
        last_signal_time_[symbol] = std::chrono::steady_clock::now();
    }
}

double EnhancedMomentumStrategy::calculate_momentum_score(const std::vector<double>& changes) const {
    if (changes.empty()) return 0.0;
    
    // Simple momentum: sum of recent price changes
    return std::accumulate(changes.begin(), changes.end(), 0.0);
}

double EnhancedMomentumStrategy::calculate_signal_confidence(double momentum, double flow) const {
    // Combine momentum and flow signals
    double combined = std::abs(momentum) + std::abs(flow);
    return std::min(combined, 1.0);
}

uint32_t EnhancedMomentumStrategy::calculate_signal_size(double confidence) const {
    double multiplier = 1.0 + (confidence * (params_.max_signal_multiplier - 1.0));
    return static_cast<uint32_t>(params_.base_signal_size * multiplier);
}

// StrategyFactory Implementation
std::unique_ptr<OrderBookStrategy> StrategyFactory::create_strategy(
    StrategyType type, uint64_t strategy_id) {
    
    switch (type) {
        case StrategyType::MARKET_MAKING:
            return std::make_unique<MarketMakingStrategy>(strategy_id);
        case StrategyType::STAT_ARB:
            return std::make_unique<StatArbStrategy>(strategy_id);
        case StrategyType::ENHANCED_MOMENTUM:
            return std::make_unique<EnhancedMomentumStrategy>(strategy_id);
        default:
            return nullptr;
    }
}

std::string StrategyFactory::strategy_type_to_string(StrategyType type) {
    switch (type) {
        case StrategyType::MARKET_MAKING: return "MarketMaking";
        case StrategyType::STAT_ARB: return "StatArb";
        case StrategyType::ENHANCED_MOMENTUM: return "EnhancedMomentum";
        default: return "Unknown";
    }
}

} // namespace hft