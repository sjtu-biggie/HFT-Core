#pragma once

#include "../common/message_types.h"
#include "../common/order_book.h"
#include "../common/logging.h"
#include <unordered_map>
#include <memory>
#include <chrono>

namespace hft {

// Enhanced strategy base class with order book support
class OrderBookStrategy {
public:
    explicit OrderBookStrategy(uint64_t strategy_id, const std::string& name)
        : strategy_id_(strategy_id), strategy_name_(name), logger_(name) {}
    
    virtual ~OrderBookStrategy() = default;

    // Strategy lifecycle
    virtual bool initialize() = 0;
    virtual void on_market_data(const MarketData& data) = 0;
    virtual void on_order_book_update(const OrderBookUpdate& update) = 0;
    virtual void on_execution(const OrderExecution& execution) = 0;
    
    uint64_t get_strategy_id() const { return strategy_id_; }
    const std::string& get_name() const { return strategy_name_; }

protected:
    uint64_t strategy_id_;
    std::string strategy_name_;
    Logger logger_;
};

// Market making strategy using order book depth
class MarketMakingStrategy : public OrderBookStrategy {
public:
    explicit MarketMakingStrategy(uint64_t strategy_id);
    ~MarketMakingStrategy() override = default;

    bool initialize() override;
    void on_market_data(const MarketData& data) override;
    void on_order_book_update(const OrderBookUpdate& update) override;
    void on_execution(const OrderExecution& execution) override;

    // Strategy parameters
    struct Parameters {
        double spread_threshold = 0.001;    // Minimum spread to start market making
        double quote_size_ratio = 0.1;      // Quote size as ratio of best level
        double max_inventory = 1000.0;      // Maximum position size
        double inventory_skew_factor = 0.5; // How much to skew quotes based on inventory
        uint32_t min_quote_size = 100;      // Minimum quote size
        uint32_t max_quote_size = 500;      // Maximum quote size
    };

private:
    Parameters params_;
    OrderBookManager book_manager_;
    std::unordered_map<std::string, double> positions_;  // Current positions by symbol
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_quote_time_;
    
    static constexpr auto MIN_QUOTE_INTERVAL = std::chrono::milliseconds(100);
    
    // Strategy logic
    void evaluate_market_making_opportunity(const std::string& symbol);
    void generate_quotes(const std::string& symbol, const OrderBook* book);
    double calculate_fair_value(const OrderBook* book) const;
    double calculate_quote_skew(const std::string& symbol) const;
    bool should_quote(const std::string& symbol, const OrderBook* book) const;
};

// Statistical arbitrage strategy using order book imbalance
class StatArbStrategy : public OrderBookStrategy {
public:
    explicit StatArbStrategy(uint64_t strategy_id);
    ~StatArbStrategy() override = default;

    bool initialize() override;
    void on_market_data(const MarketData& data) override;
    void on_order_book_update(const OrderBookUpdate& update) override;
    void on_execution(const OrderExecution& execution) override;

    struct Parameters {
        double imbalance_threshold = 0.3;   // Order book imbalance threshold
        double price_threshold = 0.002;     // Price movement threshold
        uint32_t lookback_periods = 20;     // Periods for mean reversion
        uint32_t min_signal_interval_ms = 500;  // Minimum time between signals
        uint32_t signal_size = 200;         // Signal size in shares
    };

private:
    Parameters params_;
    OrderBookManager book_manager_;
    
    // Market data history for mean reversion
    struct MarketState {
        std::vector<double> mid_prices;
        std::vector<double> imbalances;
        double mean_price = 0.0;
        double mean_imbalance = 0.0;
    };
    
    std::unordered_map<std::string, MarketState> market_states_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_signal_time_;
    
    // Strategy logic
    void update_market_state(const std::string& symbol, const OrderBook* book);
    void evaluate_stat_arb_signal(const std::string& symbol);
    double calculate_z_score(const std::vector<double>& data, double current_value) const;
    bool should_generate_signal(const std::string& symbol) const;
};

// Momentum strategy enhanced with order book flow
class EnhancedMomentumStrategy : public OrderBookStrategy {
public:
    explicit EnhancedMomentumStrategy(uint64_t strategy_id);
    ~EnhancedMomentumStrategy() override = default;

    bool initialize() override;
    void on_market_data(const MarketData& data) override;
    void on_order_book_update(const OrderBookUpdate& update) override;
    void on_execution(const OrderExecution& execution) override;

    struct Parameters {
        double momentum_threshold = 0.01;   // Price momentum threshold
        double flow_threshold = 0.2;        // Order flow imbalance threshold
        uint32_t momentum_window = 10;      // Momentum calculation window
        uint32_t min_signal_interval_ms = 1000;  // Minimum time between signals
        uint32_t base_signal_size = 100;    // Base signal size
        double max_signal_multiplier = 3.0; // Max multiplier based on conviction
    };

private:
    Parameters params_;
    OrderBookManager book_manager_;
    
    struct MomentumState {
        std::vector<double> price_changes;
        std::vector<double> flow_imbalances;
        double last_mid_price = 0.0;
        std::chrono::steady_clock::time_point last_update;
    };
    
    std::unordered_map<std::string, MomentumState> momentum_states_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_signal_time_;
    
    // Strategy logic
    void update_momentum_state(const std::string& symbol, double mid_price, double imbalance);
    void evaluate_momentum_signal(const std::string& symbol);
    double calculate_momentum_score(const std::vector<double>& changes) const;
    double calculate_signal_confidence(double momentum, double flow) const;
    uint32_t calculate_signal_size(double confidence) const;
};

// Strategy factory for creating different strategy types
enum class StrategyType {
    MARKET_MAKING,
    STAT_ARB,
    ENHANCED_MOMENTUM
};

class StrategyFactory {
public:
    static std::unique_ptr<OrderBookStrategy> create_strategy(
        StrategyType type, uint64_t strategy_id);
    
    static std::string strategy_type_to_string(StrategyType type);
};

} // namespace hft