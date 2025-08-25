#include "order_book.h"
#include "logging.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace hft {

// OrderBook Implementation
OrderBook::OrderBook(const std::string& symbol)
    : symbol_(symbol)
    , last_update_time_(0)
    , last_sequence_number_(0) {
}

void OrderBook::apply_update(const OrderBookUpdate& update) {
    // Validate sequence number (basic gap detection)
    if (last_sequence_number_ != 0 && update.sequence_number <= last_sequence_number_) {
        // Log warning about out-of-order update
        return;
    }
    
    last_sequence_number_ = update.sequence_number;
    last_update_time_ = update.exchange_timestamp;
    
    if (update.side == BookSide::BID) {
        update_level(bids_, update.level, update.update_type);
    } else {
        update_level(asks_, update.level, update.update_type);
    }
}

void OrderBook::apply_snapshot(const std::vector<OrderBookLevel>& bids,
                              const std::vector<OrderBookLevel>& asks) {
    // Clear existing book
    bids_.clear();
    asks_.clear();
    
    // Populate bids (should be sorted highest to lowest)
    for (const auto& level : bids) {
        if (level.size > 0) {
            bids_[level.price] = level;
        }
    }
    
    // Populate asks (should be sorted lowest to highest)  
    for (const auto& level : asks) {
        if (level.size > 0) {
            asks_[level.price] = level;
        }
    }
}

double OrderBook::get_best_bid() const {
    return bids_.empty() ? 0.0 : bids_.begin()->first;
}

double OrderBook::get_best_ask() const {
    return asks_.empty() ? 0.0 : asks_.begin()->first;
}

double OrderBook::get_mid_price() const {
    double best_bid = get_best_bid();
    double best_ask = get_best_ask();
    
    if (best_bid > 0.0 && best_ask > 0.0) {
        return (best_bid + best_ask) / 2.0;
    }
    return 0.0;
}

double OrderBook::get_spread() const {
    double best_bid = get_best_bid();
    double best_ask = get_best_ask();
    
    if (best_bid > 0.0 && best_ask > 0.0) {
        return best_ask - best_bid;
    }
    return 0.0;
}

uint32_t OrderBook::get_bid_size_at_level(size_t level) const {
    if (level >= bids_.size()) return 0;
    
    auto it = bids_.begin();
    std::advance(it, level);
    return it->second.size;
}

uint32_t OrderBook::get_ask_size_at_level(size_t level) const {
    if (level >= asks_.size()) return 0;
    
    auto it = asks_.begin();
    std::advance(it, level);
    return it->second.size;
}

double OrderBook::get_volume_weighted_price(BookSide side, uint32_t shares) const {
    const auto& book = (side == BookSide::BID) ? 
        reinterpret_cast<const std::map<double, OrderBookLevel>&>(bids_) : asks_;
    
    if (book.empty() || shares == 0) return 0.0;
    
    uint32_t remaining_shares = shares;
    double total_cost = 0.0;
    uint32_t total_shares = 0;
    
    for (const auto& [price, level] : book) {
        uint32_t take_size = std::min(remaining_shares, level.size);
        total_cost += price * take_size;
        total_shares += take_size;
        remaining_shares -= take_size;
        
        if (remaining_shares == 0) break;
    }
    
    return total_shares > 0 ? total_cost / total_shares : 0.0;
}

double OrderBook::get_market_impact(BookSide side, uint32_t shares) const {
    if (shares == 0) return 0.0;
    
    double current_price = (side == BookSide::BID) ? get_best_bid() : get_best_ask();
    double vwap = get_volume_weighted_price(side, shares);
    
    if (current_price > 0.0 && vwap > 0.0) {
        return std::abs(vwap - current_price) / current_price;
    }
    
    return 0.0;
}

uint32_t OrderBook::get_total_size(BookSide side, size_t levels) const {
    const auto& book = (side == BookSide::BID) ? 
        reinterpret_cast<const std::map<double, OrderBookLevel>&>(bids_) : asks_;
    
    uint32_t total = 0;
    size_t count = 0;
    
    for (const auto& [price, level] : book) {
        if (count >= levels) break;
        total += level.size;
        ++count;
    }
    
    return total;
}

double OrderBook::get_bid_ask_imbalance() const {
    uint32_t bid_size = get_bid_size_at_level(0);  // Best bid size
    uint32_t ask_size = get_ask_size_at_level(0);  // Best ask size
    
    if (bid_size + ask_size == 0) return 0.0;
    
    return static_cast<double>(bid_size - ask_size) / (bid_size + ask_size);
}

size_t OrderBook::get_book_depth(BookSide side) const {
    return (side == BookSide::BID) ? bids_.size() : asks_.size();
}

bool OrderBook::is_valid() const {
    // Basic validation: best bid < best ask
    double best_bid = get_best_bid();
    double best_ask = get_best_ask();
    
    if (best_bid > 0.0 && best_ask > 0.0) {
        return best_bid < best_ask;
    }
    
    // Valid if we have at least one side
    return !bids_.empty() || !asks_.empty();
}

// Helper methods
void OrderBook::update_level(std::map<double, OrderBookLevel, std::greater<double>>& book, 
                            const OrderBookLevel& level, BookUpdateType type) {
    switch (type) {
        case BookUpdateType::ADD:
        case BookUpdateType::UPDATE:
            if (level.size > 0) {
                book[level.price] = level;
            } else {
                book.erase(level.price);  // Zero size = delete
            }
            break;
            
        case BookUpdateType::DELETE:
            book.erase(level.price);
            break;
            
        case BookUpdateType::SNAPSHOT:
            // Snapshot should use apply_snapshot method
            break;
    }
}

void OrderBook::update_level(std::map<double, OrderBookLevel>& book, 
                            const OrderBookLevel& level, BookUpdateType type) {
    switch (type) {
        case BookUpdateType::ADD:
        case BookUpdateType::UPDATE:
            if (level.size > 0) {
                book[level.price] = level;
            } else {
                book.erase(level.price);  // Zero size = delete
            }
            break;
            
        case BookUpdateType::DELETE:
            book.erase(level.price);
            break;
            
        case BookUpdateType::SNAPSHOT:
            // Snapshot should use apply_snapshot method
            break;
    }
}

// OrderBookManager Implementation
void OrderBookManager::add_symbol(const std::string& symbol) {
    if (books_.find(symbol) == books_.end()) {
        books_[symbol] = std::make_unique<OrderBook>(symbol);
    }
}

OrderBook* OrderBookManager::get_book(const std::string& symbol) {
    auto it = books_.find(symbol);
    return (it != books_.end()) ? it->second.get() : nullptr;
}

const OrderBook* OrderBookManager::get_book(const std::string& symbol) const {
    auto it = books_.find(symbol);
    return (it != books_.end()) ? it->second.get() : nullptr;
}

void OrderBookManager::process_update(const OrderBookUpdate& update) {
    std::string symbol(update.symbol);
    
    // Auto-create book if it doesn't exist
    if (books_.find(symbol) == books_.end()) {
        add_symbol(symbol);
    }
    
    auto* book = get_book(symbol);
    if (book) {
        book->apply_update(update);
    }
}

std::vector<std::string> OrderBookManager::get_symbols() const {
    std::vector<std::string> symbols;
    symbols.reserve(books_.size());
    
    for (const auto& [symbol, book] : books_) {
        symbols.push_back(symbol);
    }
    
    return symbols;
}

// OrderBookFactory Implementation
OrderBookUpdate OrderBookFactory::create_level_update(const std::string& symbol,
                                                      BookSide side,
                                                      BookUpdateType type,
                                                      double price,
                                                      uint32_t size,
                                                      uint64_t seq_num,
                                                      uint32_t order_count) {
    OrderBookUpdate update;
    update.header = MessageFactory::create_header(MessageType::ORDER_BOOK_UPDATE, 
                                                 sizeof(OrderBookUpdate) - sizeof(MessageHeader));
    
    // Copy symbol (ensure null termination)
    std::strncpy(update.symbol, symbol.c_str(), sizeof(update.symbol) - 1);
    update.symbol[sizeof(update.symbol) - 1] = '\0';
    
    update.update_type = type;
    update.side = side;
    update.level = OrderBookLevel(price, size, order_count);
    update.sequence_number = seq_num;
    update.exchange_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    return update;
}

std::string OrderBookFactory::update_to_string(const OrderBookUpdate& update) {
    std::string result = "OrderBookUpdate{";
    result += "symbol=" + std::string(update.symbol);
    result += ", side=" + std::string(update.side == BookSide::BID ? "BID" : "ASK");
    result += ", type=" + std::to_string(static_cast<int>(update.update_type));
    result += ", price=" + std::to_string(update.level.price);
    result += ", size=" + std::to_string(update.level.size);
    result += ", seq=" + std::to_string(update.sequence_number);
    result += "}";
    return result;
}

} // namespace hft