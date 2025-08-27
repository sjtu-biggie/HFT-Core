#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include <memory>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <functional>
#include <atomic>

namespace hft {

// Fill simulation models
enum class FillModel {
    IMMEDIATE,          // Fill immediately at order price
    REALISTIC_SLIPPAGE, // Fill with realistic market slippage
    MARKET_IMPACT,      // Fill considering market impact based on order size
    LATENCY_AWARE,      // Fill with simulated network/exchange latency
    PARTIAL_FILLS       // Support partial fills over time
};

// Market state for realistic simulation
struct MarketState {
    std::string symbol;
    double bid_price;
    double ask_price;
    double last_price;
    uint64_t bid_volume;
    uint64_t ask_volume;
    double spread;
    double volatility;
    timestamp_t timestamp;
    
    double mid_price() const { 
        return (bid_price + ask_price) / 2.0; 
    }
    
    double spread_bps() const {
        return ((ask_price - bid_price) / mid_price()) * 10000.0;
    }
};

// Order fill event for delayed processing
struct FillEvent {
    uint64_t order_id;
    double fill_price;
    uint32_t fill_quantity;
    timestamp_t fill_time;
    ExecutionType exec_type;
    
    bool operator>(const FillEvent& other) const {
        return fill_time > other.fill_time;
    }
};

// Fill configuration
struct FillConfig {
    FillModel model = FillModel::REALISTIC_SLIPPAGE;
    double slippage_factor = 0.001;      // 0.1% default slippage
    double market_impact_factor = 0.0001; // Impact per share
    int min_latency_ms = 1;              // Minimum fill latency
    int max_latency_ms = 50;             // Maximum fill latency
    double partial_fill_probability = 0.1; // 10% chance of partial fill
    bool enable_spread_crossing = true;   // Allow orders to cross spread
    double volatility_impact = 0.5;      // How much volatility affects fills
    
    // Transaction cost model
    double commission_per_share = 0.0;
    double commission_percentage = 0.0;
    double minimum_commission = 0.0;
    
    // Market hours
    bool respect_market_hours = false;
    std::string market_open_time = "09:30:00";
    std::string market_close_time = "16:00:00";
};

// Callback for fill notifications
using FillCallback = std::function<void(const OrderExecution&)>;

class FillSimulator {
public:
    FillSimulator();
    ~FillSimulator();
    
    // Configuration
    bool initialize(const FillConfig& config);
    void set_fill_callback(FillCallback callback) { fill_callback_ = callback; }
    
    // Market data updates
    void update_market_state(const MarketData& market_data);
    
    // Order processing
    void submit_order(uint64_t order_id, const std::string& symbol, 
                     SignalAction action, OrderType type, 
                     double price, uint32_t quantity);
    void cancel_order(uint64_t order_id);
    
    // Processing
    void process_pending_fills();
    bool has_pending_orders() const { return !pending_orders_.empty(); }
    
    // Statistics
    uint64_t get_total_fills() const { return total_fills_; }
    uint64_t get_partial_fills() const { return partial_fills_; }
    double get_average_slippage() const;
    double get_total_commission() const { return total_commission_; }
    
    // Market simulation
    void set_volatility_model(const std::string& symbol, double volatility);
    void enable_realistic_spreads(bool enable) { realistic_spreads_ = enable; }

private:
    struct PendingOrder {
        uint64_t order_id;
        std::string symbol;
        SignalAction action;
        OrderType type;
        double price;
        uint32_t quantity;
        uint32_t filled_quantity;
        timestamp_t submit_time;
        timestamp_t last_update;
    };
    
    FillConfig config_;
    Logger logger_;
    FillCallback fill_callback_;
    
    // Order management
    std::unordered_map<uint64_t, PendingOrder> pending_orders_;
    std::priority_queue<FillEvent, std::vector<FillEvent>, std::greater<FillEvent>> fill_queue_;
    
    // Market state
    std::unordered_map<std::string, MarketState> market_states_;
    std::unordered_map<std::string, double> symbol_volatilities_;
    bool realistic_spreads_;
    
    // Statistics
    std::atomic<uint64_t> total_fills_;
    std::atomic<uint64_t> partial_fills_;
    double total_slippage_;
    double total_commission_;
    
    // Fill simulation methods
    void process_order_fill(PendingOrder& order);
    FillEvent calculate_fill_event(const PendingOrder& order, const MarketState& market);
    double calculate_fill_price(const PendingOrder& order, const MarketState& market);
    uint32_t calculate_fill_quantity(const PendingOrder& order, const MarketState& market);
    double calculate_slippage(const PendingOrder& order, const MarketState& market);
    double calculate_market_impact(const PendingOrder& order, const MarketState& market);
    int calculate_latency(const PendingOrder& order);
    double calculate_commission(double fill_price, uint32_t fill_quantity);
    
    // Market simulation helpers
    bool is_market_open(timestamp_t timestamp);
    double generate_realistic_spread(const std::string& symbol, double price);
    void update_volatility(const std::string& symbol, double price_change);
    
    // Utility methods
    timestamp_t current_time();
    double random_normal(double mean, double stddev);
    double random_uniform(double min, double max);
};

} // namespace hft