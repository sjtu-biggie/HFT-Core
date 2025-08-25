#include "../common/message_types.h"
#include <cassert>
#include <iostream>
#include <cstring>

using namespace hft;

void test_message_header_creation() {
    std::cout << "Testing message header creation..." << std::endl;
    
    auto header = MessageFactory::create_header(MessageType::MARKET_DATA, 100);
    
    assert(header.type == MessageType::MARKET_DATA);
    assert(header.payload_size == 100);
    assert(header.sequence_number > 0);
    assert(header.timestamp.count() > 0);
    
    std::cout << "✓ Message header creation test passed" << std::endl;
}

void test_market_data_creation() {
    std::cout << "Testing market data message creation..." << std::endl;
    
    MarketData data = MessageFactory::create_market_data(
        "AAPL", 150.0, 150.5, 1000, 800, 150.25, 500
    );
    
    assert(data.header.type == MessageType::MARKET_DATA);
    assert(std::strcmp(data.symbol, "AAPL") == 0);
    assert(data.bid_price == 150.0);
    assert(data.ask_price == 150.5);
    assert(data.bid_size == 1000);
    assert(data.ask_size == 800);
    assert(data.last_price == 150.25);
    assert(data.last_size == 500);
    
    std::cout << "✓ Market data creation test passed" << std::endl;
}

void test_trading_signal_creation() {
    std::cout << "Testing trading signal creation..." << std::endl;
    
    TradingSignal signal = MessageFactory::create_trading_signal(
        "GOOGL", SignalAction::BUY, OrderType::LIMIT, 2800.0, 100, 1001, 0.85
    );
    
    assert(signal.header.type == MessageType::TRADING_SIGNAL);
    assert(std::strcmp(signal.symbol, "GOOGL") == 0);
    assert(signal.action == SignalAction::BUY);
    assert(signal.order_type == OrderType::LIMIT);
    assert(signal.price == 2800.0);
    assert(signal.quantity == 100);
    assert(signal.strategy_id == 1001);
    assert(signal.confidence == 0.85);
    
    std::cout << "✓ Trading signal creation test passed" << std::endl;
}

void test_log_message_creation() {
    std::cout << "Testing log message creation..." << std::endl;
    
    LogMessage log = MessageFactory::create_log_message(
        LogLevel::INFO, "TestComponent", "Test message"
    );
    
    assert(log.header.type == MessageType::LOG_MESSAGE);
    assert(log.level == LogLevel::INFO);
    assert(std::strcmp(log.component, "TestComponent") == 0);
    assert(std::strcmp(log.message, "Test message") == 0);
    
    std::cout << "✓ Log message creation test passed" << std::endl;
}

void test_message_validation() {
    std::cout << "Testing message validation..." << std::endl;
    
    // Valid market data
    MarketData valid_data = MessageFactory::create_market_data(
        "TSLA", 200.0, 201.0, 500, 600, 200.5, 100
    );
    Message valid_msg;
    valid_msg.market_data = valid_data;
    assert(MessageFactory::validate_message(valid_msg) == true);
    
    // Invalid market data (ask < bid)
    MarketData invalid_data = MessageFactory::create_market_data(
        "TSLA", 201.0, 200.0, 500, 600, 200.5, 100
    );
    Message invalid_msg;
    invalid_msg.market_data = invalid_data;
    assert(MessageFactory::validate_message(invalid_msg) == false);
    
    std::cout << "✓ Message validation test passed" << std::endl;
}

void test_message_to_string() {
    std::cout << "Testing message to string conversion..." << std::endl;
    
    MarketData data = MessageFactory::create_market_data(
        "META", 300.0, 300.1, 1000, 1200, 300.05, 250
    );
    Message msg;
    msg.market_data = data;
    
    std::string str = MessageFactory::message_to_string(msg);
    assert(str.find("META") != std::string::npos);
    assert(str.find("bid=300") != std::string::npos);
    assert(str.find("ask=300.1") != std::string::npos);
    
    std::cout << "✓ Message to string test passed" << std::endl;
}

void test_message_sizes() {
    std::cout << "Testing message sizes for performance..." << std::endl;
    
    // Verify message sizes are reasonable for performance
    std::cout << "MessageHeader size: " << sizeof(MessageHeader) << " bytes" << std::endl;
    std::cout << "MarketData size: " << sizeof(MarketData) << " bytes" << std::endl;
    std::cout << "TradingSignal size: " << sizeof(TradingSignal) << " bytes" << std::endl;
    std::cout << "OrderExecution size: " << sizeof(OrderExecution) << " bytes" << std::endl;
    std::cout << "LogMessage size: " << sizeof(LogMessage) << " bytes" << std::endl;
    
    // Ensure messages are reasonably sized (under 1KB for performance)
    assert(sizeof(MarketData) < 1024);
    assert(sizeof(TradingSignal) < 1024);
    assert(sizeof(OrderExecution) < 1024);
    
    std::cout << "✓ Message sizes test passed" << std::endl;
}

int main() {
    std::cout << "Running Message Types Unit Tests" << std::endl;
    std::cout << "================================" << std::endl;
    
    try {
        test_message_header_creation();
        test_market_data_creation();
        test_trading_signal_creation();
        test_log_message_creation();
        test_message_validation();
        test_message_to_string();
        test_message_sizes();
        
        std::cout << "\n✅ All message types tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}