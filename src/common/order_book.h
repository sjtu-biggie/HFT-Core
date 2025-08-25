#pragma once

#include "message_types.h"
#include <map>
#include <vector>
#include <memory>
#include <string>

namespace hft {

// Level 2 market data - order book depth
struct OrderBookLevel {
    double price;
    uint32_t size;
    uint32_t order_count;  // Number of orders at this level
    
    OrderBookLevel(double p = 0.0, uint32_t s = 0, uint32_t c = 0)
        : price(p), size(s), order_count(c) {}
} __attribute__((packed));

// Order book update message
enum class BookUpdateType : uint8_t {
    ADD = 1,        // New level added
    UPDATE = 2,     // Level size changed  
    DELETE = 3,     // Level removed
    SNAPSHOT = 4    // Full book snapshot
};

enum class BookSide : uint8_t {
    BID = 1,
    ASK = 2
};

struct OrderBookUpdate {
    MessageHeader header;
    char symbol[16];
    BookUpdateType update_type;
    BookSide side;
    OrderBookLevel level;
    uint64_t sequence_number;    // Exchange sequence number
    uint64_t exchange_timestamp; // Exchange timestamp
} __attribute__((packed));

// In-memory order book representation
class OrderBook {
public:
    explicit OrderBook(const std::string& symbol);
    ~OrderBook() = default;

    // Process order book updates
    void apply_update(const OrderBookUpdate& update);
    void apply_snapshot(const std::vector<OrderBookLevel>& bids,
                       const std::vector<OrderBookLevel>& asks);

    // Query methods
    double get_best_bid() const;
    double get_best_ask() const;
    double get_mid_price() const;
    double get_spread() const;
    uint32_t get_bid_size_at_level(size_t level) const;  // 0 = best
    uint32_t get_ask_size_at_level(size_t level) const;
    
    // Advanced queries for strategies
    double get_volume_weighted_price(BookSide side, uint32_t shares) const;
    double get_market_impact(BookSide side, uint32_t shares) const;
    uint32_t get_total_size(BookSide side, size_t levels = 5) const;
    
    // Book quality metrics
    double get_bid_ask_imbalance() const;  // (bid_size - ask_size) / (bid_size + ask_size)
    size_t get_book_depth(BookSide side) const;
    
    // Validation
    bool is_valid() const;
    uint64_t get_last_update_time() const { return last_update_time_; }
    
    const std::string& symbol() const { return symbol_; }

private:
    std::string symbol_;
    
    // Ordered maps: price -> level (bids descending, asks ascending)
    std::map<double, OrderBookLevel, std::greater<double>> bids_;  // Best bid = highest price
    std::map<double, OrderBookLevel> asks_;                       // Best ask = lowest price
    
    uint64_t last_update_time_;
    uint64_t last_sequence_number_;
    
    // Helper methods
    void update_level(std::map<double, OrderBookLevel, std::greater<double>>& book, 
                     const OrderBookLevel& level, BookUpdateType type);
    void update_level(std::map<double, OrderBookLevel>& book, 
                     const OrderBookLevel& level, BookUpdateType type);
};

// Order book manager - handles multiple symbols
class OrderBookManager {
public:
    OrderBookManager() = default;
    ~OrderBookManager() = default;

    // Book management
    void add_symbol(const std::string& symbol);
    OrderBook* get_book(const std::string& symbol);
    const OrderBook* get_book(const std::string& symbol) const;
    
    // Process updates
    void process_update(const OrderBookUpdate& update);
    
    // Statistics
    size_t get_book_count() const { return books_.size(); }
    std::vector<std::string> get_symbols() const;

private:
    std::map<std::string, std::unique_ptr<OrderBook>> books_;
};

// Helper functions for creating order book messages
class OrderBookFactory {
public:
    static OrderBookUpdate create_level_update(const std::string& symbol,
                                              BookSide side,
                                              BookUpdateType type,
                                              double price,
                                              uint32_t size,
                                              uint64_t seq_num = 0,
                                              uint32_t order_count = 1);
    
    static std::string update_to_string(const OrderBookUpdate& update);
};

} // namespace hft