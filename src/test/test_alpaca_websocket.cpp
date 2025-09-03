#include "../market_data_handler/alpaca_market_data.h"
#include "../common/message_types.h"
#include "../common/static_config.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

using namespace hft;

class AlpacaWebSocketTester {
public:
    AlpacaWebSocketTester() 
        : messages_received_(0)
        , test_duration_(30) // 30 seconds test
        , test_passed_(false) {
    }
    
    bool run_test(const std::string& api_key, const std::string& api_secret) {
        std::cout << "=== Alpaca WebSocket Integration Test ===" << std::endl;
        std::cout << "Testing with Boost.Beast implementation" << std::endl;
        
        // Load configuration from file
        StaticConfig::load_from_file("config/hft_config.conf");
        
        // Initialize Alpaca client with WebSocket configuration from config
        std::string websocket_url = StaticConfig::get_alpaca_websocket_url();
        std::string websocket_host = StaticConfig::get_alpaca_websocket_host();
        alpaca_client_.initialize(api_key, api_secret, websocket_url, websocket_host, true); // Paper trading
        
        // Set up callback to receive data
        alpaca_client_.set_data_callback([this](const MarketData& data) {
            handle_market_data(data);
        });
        
        std::cout << "1. Starting Alpaca client..." << std::endl;
        alpaca_client_.start();
        
        std::cout << "2. Connecting to Alpaca WebSocket..." << std::endl;
        if (!alpaca_client_.connect()) {
            std::cerr << "❌ Failed to connect to Alpaca WebSocket" << std::endl;
            return false;
        }
        std::cout << "✅ Connected successfully" << std::endl;
        
        // Get symbols from configuration
        const std::vector<std::string>& test_symbols = StaticConfig::get_symbols();
        std::cout << "3. Subscribing to test symbols..." << std::endl;
        if (!alpaca_client_.subscribe(test_symbols)) {
            std::cerr << "❌ Failed to subscribe to symbols" << std::endl;
            return false;
        }
        std::cout << "✅ Subscribed to " << test_symbols.size() << " symbols" << std::endl;
        
        std::cout << "4. Waiting for market data (max " << test_duration_ << " seconds)..." << std::endl;
        
        // Wait for data or timeout
        auto start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(test_duration_)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
            
            std::cout << "   Time: " << elapsed << "s, Messages: " 
                      << messages_received_.load() << std::endl;
            
            // Test passes if we receive at least 5 messages
            if (messages_received_.load() >= 5) {
                test_passed_ = true;
                std::cout << "✅ Received sufficient market data messages!" << std::endl;
                break;
            }
        }
        
        std::cout << "5. Disconnecting..." << std::endl;
        alpaca_client_.stop();
        
        // Print final stats
        print_test_results();
        
        return test_passed_;
    }
    
private:
    void handle_market_data(const MarketData& data) {
        messages_received_++;
        
        // Log every 10th message to avoid spam
        if (messages_received_.load() % 10 == 1) {
            std::cout << "   📈 Market Data: " << data.symbol 
                      << " bid=" << data.bid_price 
                      << " ask=" << data.ask_price 
                      << " last=" << data.last_price << std::endl;
        }
    }
    
    void print_test_results() {
        const auto& metrics = alpaca_client_.get_metrics();
        
        std::cout << std::endl;
        std::cout << "=== Test Results ===" << std::endl;
        std::cout << "Messages received by callback: " << messages_received_.load() << std::endl;
        std::cout << "Raw messages received: " << metrics.messages_received.load() << std::endl;
        std::cout << "Messages processed: " << metrics.messages_processed.load() << std::endl;
        std::cout << "Quotes processed: " << metrics.quotes_processed.load() << std::endl;
        std::cout << "Trades processed: " << metrics.trades_processed.load() << std::endl;
        std::cout << "Bars processed: " << metrics.bars_processed.load() << std::endl;
        std::cout << "Parse errors: " << metrics.parse_errors.load() << std::endl;
        std::cout << "Connection errors: " << metrics.connection_errors.load() << std::endl;
        std::cout << "Bytes received: " << metrics.bytes_received.load() << std::endl;
        std::cout << "Average latency: " << metrics.get_average_latency_microseconds() << " μs" << std::endl;
        std::cout << "Connection healthy: " << (alpaca_client_.is_healthy() ? "Yes" : "No") << std::endl;
        std::cout << std::endl;
        
        if (test_passed_) {
            std::cout << "🎉 TEST PASSED - WebSocket connection and data reception working!" << std::endl;
        } else {
            std::cout << "❌ TEST FAILED - No market data received or connection issues" << std::endl;
            
            // Suggestions for debugging
            std::cout << std::endl;
            std::cout << "Debugging suggestions:" << std::endl;
            std::cout << "1. Check your Alpaca API credentials" << std::endl;
            std::cout << "2. Verify network connectivity to stream.data.alpaca.markets" << std::endl;
            std::cout << "3. Check if market is open (IEX data is only available during market hours)" << std::endl;
            std::cout << "4. Review the logs above for specific error messages" << std::endl;
        }
        
        std::cout << "===================" << std::endl;
    }
    
    AlpacaMarketData alpaca_client_;
    std::atomic<int> messages_received_;
    int test_duration_;
    bool test_passed_;
};

int main(int argc, char* argv[]) {
    std::string api_key = "PK59N6S7LY64KT7AIMJ6";
    std::string api_secret = "ZtJln5SpStjo9CefsegyqeUBsz8zDDc1FCpjaO3R";
    
    // Allow override from command line
    if (argc >= 3) {
        api_key = argv[1];
        api_secret = argv[2];
        std::cout << "Using API credentials from command line" << std::endl;
    } else {
        std::cout << "Using default test credentials (may not work with real Alpaca)" << std::endl;
        std::cout << "Usage: " << argv[0] << " <api_key> <api_secret>" << std::endl;
        std::cout << "Proceeding with test anyway..." << std::endl;
        std::cout << std::endl;
    }
    
    AlpacaWebSocketTester tester;
    bool success = tester.run_test(api_key, api_secret);
    
    return success ? 0 : 1;
}