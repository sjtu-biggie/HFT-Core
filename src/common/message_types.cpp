#include "message_types.h"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace hft {

static uint32_t g_sequence_number = 0;

MessageHeader MessageFactory::create_header(MessageType type, uint16_t payload_size) {
    MessageHeader header;
    header.type = type;
    header.sequence_number = ++g_sequence_number;
    header.timestamp = std::chrono::duration_cast<timestamp_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    header.payload_size = payload_size;
    return header;
}

MarketData MessageFactory::create_market_data(const std::string& symbol,
                                             double bid, double ask,
                                             uint32_t bid_size, uint32_t ask_size,
                                             double last_price, uint32_t last_size) {
    MarketData data;
    data.header = create_header(MessageType::MARKET_DATA, sizeof(MarketData) - sizeof(MessageHeader));
    
    std::memset(data.symbol, 0, sizeof(data.symbol));
    std::strncpy(data.symbol, symbol.c_str(), sizeof(data.symbol) - 1);
    
    data.bid_price = bid;
    data.ask_price = ask;
    data.bid_size = bid_size;
    data.ask_size = ask_size;
    data.last_price = last_price;
    data.last_size = last_size;
    data.exchange_timestamp = data.header.timestamp.count();
    
    return data;
}

TradingSignal MessageFactory::create_trading_signal(const std::string& symbol,
                                                   SignalAction action,
                                                   OrderType type,
                                                   double price,
                                                   uint32_t quantity,
                                                   uint64_t strategy_id,
                                                   double confidence) {
    TradingSignal signal;
    signal.header = create_header(MessageType::TRADING_SIGNAL, sizeof(TradingSignal) - sizeof(MessageHeader));
    
    std::memset(signal.symbol, 0, sizeof(signal.symbol));
    std::strncpy(signal.symbol, symbol.c_str(), sizeof(signal.symbol) - 1);
    
    signal.action = action;
    signal.order_type = type;
    signal.price = price;
    signal.quantity = quantity;
    signal.strategy_id = strategy_id;
    signal.confidence = confidence;
    
    return signal;
}

LogMessage MessageFactory::create_log_message(LogLevel level,
                                             const std::string& component,
                                             const std::string& message) {
    LogMessage log;
    log.header = create_header(MessageType::LOG_MESSAGE, sizeof(LogMessage) - sizeof(MessageHeader));
    
    log.level = level;
    
    std::memset(log.component, 0, sizeof(log.component));
    std::strncpy(log.component, component.c_str(), sizeof(log.component) - 1);
    
    std::memset(log.message, 0, sizeof(log.message));
    std::strncpy(log.message, message.c_str(), sizeof(log.message) - 1);
    
    return log;
}

bool MessageFactory::validate_message(const Message& msg) {
    // Basic validation of message structure
    if (msg.header.payload_size == 0) {
        return false;
    }
    
    // Validate timestamp is reasonable (not too far in past/future)
    auto now = std::chrono::duration_cast<timestamp_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    );
    auto age = now - msg.header.timestamp;
    if (age > std::chrono::seconds(60) || age < std::chrono::seconds(-1)) {
        return false;
    }
    
    // Type-specific validation
    switch (msg.header.type) {
        case MessageType::MARKET_DATA:
            return msg.market_data.bid_price > 0 && 
                   msg.market_data.ask_price > 0 &&
                   msg.market_data.ask_price >= msg.market_data.bid_price;
        
        case MessageType::TRADING_SIGNAL:
            return msg.trading_signal.quantity > 0 &&
                   msg.trading_signal.confidence >= 0.0 &&
                   msg.trading_signal.confidence <= 1.0;
        
        default:
            return true; // Basic validation passed
    }
}

std::string MessageFactory::message_to_string(const Message& msg) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    
    oss << "Message[seq=" << msg.header.sequence_number 
        << ", ts=" << msg.header.timestamp.count() << "ns, ";
    
    switch (msg.header.type) {
        case MessageType::MARKET_DATA:
            oss << "MARKET_DATA: " << msg.market_data.symbol
                << " bid=" << msg.market_data.bid_price << "x" << msg.market_data.bid_size
                << " ask=" << msg.market_data.ask_price << "x" << msg.market_data.ask_size
                << " last=" << msg.market_data.last_price;
            break;
            
        case MessageType::TRADING_SIGNAL:
            oss << "TRADING_SIGNAL: " << msg.trading_signal.symbol
                << " action=" << static_cast<int>(msg.trading_signal.action)
                << " price=" << msg.trading_signal.price
                << " qty=" << msg.trading_signal.quantity
                << " conf=" << msg.trading_signal.confidence;
            break;
            
        case MessageType::LOG_MESSAGE:
            oss << "LOG[" << static_cast<int>(msg.log_message.level) << "]: "
                << msg.log_message.component << " - " << msg.log_message.message;
            break;
            
        default:
            oss << "Type=" << static_cast<int>(msg.header.type);
            break;
    }
    
    oss << "]";
    return oss.str();
}

} // namespace hft