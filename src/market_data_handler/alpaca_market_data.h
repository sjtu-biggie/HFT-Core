#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/hft_metrics.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace hft {

// Alpaca market data subscription
struct AlpacaSubscription {
    std::vector<std::string> quotes;   // Stock quotes (bid/ask)
    std::vector<std::string> trades;   // Trade executions
    std::vector<std::string> bars;     // OHLCV bars
    std::string feed = "iex";          // "iex" or "sip" (paid)
};

// Simplified metrics for Alpaca data ingestion
struct AlpacaMetrics {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> quotes_processed{0};
    std::atomic<uint64_t> trades_processed{0};
    std::atomic<uint64_t> bars_processed{0};
    std::atomic<uint64_t> parse_errors{0};
    std::atomic<uint64_t> connection_errors{0};
    std::atomic<uint64_t> bytes_received{0};
    std::chrono::steady_clock::time_point last_message_time;
    
    // Latency tracking
    std::atomic<uint64_t> total_latency_microseconds{0};
    std::atomic<uint64_t> latency_samples{0};
    
    void reset() {
        messages_received = 0;
        messages_processed = 0;
        quotes_processed = 0;
        trades_processed = 0;
        bars_processed = 0;
        parse_errors = 0;
        connection_errors = 0;
        bytes_received = 0;
        total_latency_microseconds = 0;
        latency_samples = 0;
        last_message_time = std::chrono::steady_clock::now();
    }
    
    double get_average_latency_microseconds() const {
        auto samples = latency_samples.load();
        if (samples == 0) return 0.0;
        return static_cast<double>(total_latency_microseconds.load()) / samples;
    }
};

class AlpacaMarketData {
public:
    AlpacaMarketData();
    ~AlpacaMarketData();
    
    // Initialize with Alpaca credentials and WebSocket configuration
    bool initialize(const std::string& api_key, const std::string& api_secret, 
                   const std::string& websocket_url, const std::string& host,
                   bool paper_trading = true);
    
    // Connection management - simplified
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_; }
    
    // Direct subscription - no complex management
    bool subscribe(const std::vector<std::string>& symbols);
    
    // Set callback for market data
    void set_data_callback(std::function<void(const MarketData&)> callback);
    
    // Start/stop processing
    void start();
    void stop();
    
    // Comprehensive metrics access
    const AlpacaMetrics& get_metrics() const { return metrics_; }
    void reset_metrics() { metrics_.reset(); }
    
    // Log current status with detailed metrics
    void log_status() const;
    
    // Get connection health
    bool is_healthy() const;

private:
    // Credentials and configuration
    std::string api_key_;
    std::string api_secret_;
    std::string websocket_url_;
    bool paper_trading_ = true;
    
    // Connection state - simplified
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // Callback
    std::function<void(const MarketData&)> data_callback_;
    
    // Boost.Beast WebSocket connection
    std::unique_ptr<boost::asio::io_context> ioc_;
    std::unique_ptr<boost::asio::ssl::context> ssl_ctx_;
    std::unique_ptr<boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>> ws_;
    std::unique_ptr<std::thread> ws_thread_;
    boost::beast::flat_buffer buffer_;
    std::string host_;
    std::string path_;
    std::string port_ = "443";
    
    // Metrics and logging
    mutable AlpacaMetrics metrics_;
    mutable Logger logger_;
    
    // Message parsing state
    std::unordered_map<std::string, double> last_quotes_;
    
    // Boost.Beast WebSocket connection methods
    bool establish_websocket_connection();
    void websocket_thread_func();
    bool send_message(const std::string& message);
    void close_websocket_connection();
    
    // Message processing - simplified and fast
    void process_message(const std::string& message);
    void process_single_message(const nlohmann::json& msg);
    bool handle_quote_message(const std::string& message);
    bool handle_trade_message(const std::string& message);
    bool handle_bar_message(const std::string& message);
    bool handle_error_message(const std::string& message);
    
    // Utilities
    std::string create_auth_message();
    std::string create_subscription_message(const std::vector<std::string>& symbols);
    MarketData convert_alpaca_quote_to_market_data(const std::string& symbol, double bid, double ask, 
                                                   uint32_t bid_size, uint32_t ask_size);
    MarketData convert_alpaca_trade_to_market_data(const std::string& symbol, double price, uint32_t size);
    
    // Fast JSON parsing helpers
    std::string extract_json_string(const std::string& json, const std::string& key);
    double extract_json_double(const std::string& json, const std::string& key);
    uint32_t extract_json_uint(const std::string& json, const std::string& key);
    
    // Metrics helpers
    void record_latency(std::chrono::steady_clock::time_point start_time);
    void update_global_metrics();
};

} // namespace hft