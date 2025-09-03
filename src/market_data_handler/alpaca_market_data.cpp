#include "alpaca_market_data.h"
#include "../common/static_config.h"
#include "../common/hft_metrics.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace hft {

AlpacaMarketData::AlpacaMarketData()
    : logger_("AlpacaMarketData", StaticConfig::get_logger_endpoint()) {
    
    logger_.info("AlpacaMarketData client initialized with Boost.Beast");
    metrics_.reset();
    
    // Initialize curl for any HTTP requests if needed
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

AlpacaMarketData::~AlpacaMarketData() {
    stop();
    curl_global_cleanup();
}

bool AlpacaMarketData::initialize(const std::string& api_key, const std::string& api_secret, 
                                  const std::string& websocket_url, const std::string& host,
                                  bool paper_trading) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    websocket_url_ = websocket_url;
    host_ = host;
    paper_trading_ = paper_trading;
    
    // Extract path from websocket_url
    // websocket_url format: ws://host:port/path or wss://host:port/path
    size_t path_start = websocket_url_.find('/', 8); // Skip "wss://" or "ws://"
    if (path_start != std::string::npos) {
        path_ = websocket_url_.substr(path_start);
    } else {
        path_ = "/v2/test"; // Default fallback
    }
    
    logger_.info("Alpaca client initialized - Mode: " + std::string(paper_trading_ ? "Paper" : "Live"));
    logger_.info("WebSocket URL: " + websocket_url_);
    logger_.info("WebSocket Host: " + host_);
    logger_.info("WebSocket Path: " + path_);
    
    return true;
}

bool AlpacaMarketData::connect() {
    if (connected_.load()) {
        logger_.warning("Already connected to Alpaca WebSocket");
        return true;
    }
    
    logger_.info("Connecting to Alpaca market data WebSocket...");
    
    try {
        // Initialize io_context and SSL context
        ioc_ = std::make_unique<net::io_context>();
        ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
        
        // Configure SSL context
        ssl_ctx_->set_default_verify_paths();
        ssl_ctx_->set_verify_mode(ssl::verify_none); // For development - should use verify_peer in production
        
        if (!establish_websocket_connection()) {
            logger_.error("Failed to establish WebSocket connection");
            return false;
        }
        
        // Start WebSocket thread
        ws_thread_ = std::make_unique<std::thread>([this]() {
            websocket_thread_func();
        });
        
        // Give the WebSocket thread a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Send authentication message immediately after connection
        std::string auth_msg = create_auth_message();
        logger_.info("Sending authentication message: " + auth_msg);
        
        if (!send_message(auth_msg)) {
            logger_.error("Failed to send authentication message - WebSocket not ready");
            logger_.error("Connected status: " + std::string(connected_.load() ? "true" : "false"));
            logger_.error("WebSocket pointer valid: " + std::string(ws_ ? "true" : "false"));
            return false;
        }
        
        logger_.info("Authentication message sent successfully, waiting for response...");
        
        // Wait for authentication response
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        logger_.info("Successfully connected to Alpaca market data");
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("Connection failed: " + std::string(e.what()));
        metrics_.connection_errors++;
        return false;
    }
}

void AlpacaMarketData::disconnect() {
    if (!connected_.load()) return;
    
    logger_.info("Disconnecting from Alpaca WebSocket");
    
    connected_ = false;
    close_websocket_connection();
    
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_thread_->join();
    }
    
    logger_.info("Disconnected from Alpaca WebSocket");
    log_status();
}

bool AlpacaMarketData::subscribe(const std::vector<std::string>& symbols) {
    if (!connected_.load()) {
        logger_.error("Not connected to Alpaca WebSocket");
        return false;
    }
    
    if (symbols.empty()) {
        logger_.warning("Empty symbols list for subscription");
        return false;
    }
    
    logger_.debug("Subscribing to " + std::to_string(symbols.size()) + " symbols");
    
    try {
        // Create subscription message
        std::string sub_message = create_subscription_message(symbols);
        
        // Send subscription
        if (!send_message(sub_message)) {
            logger_.error("Failed to send subscription message");
            return false;
        }
        
        logger_.info("Subscription sent successfully");
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("Subscription failed: " + std::string(e.what()));
        return false;
    }
}

void AlpacaMarketData::set_data_callback(std::function<void(const MarketData&)> callback) {
    data_callback_ = callback;
    logger_.info("Market data callback set");
}

void AlpacaMarketData::start() {
    if (running_.load()) {
        logger_.warning("Already running");
        return;
    }
    
    running_ = true;
    logger_.info("Alpaca market data processing started");
}

void AlpacaMarketData::stop() {
    if (!running_.load()) return;
    
    running_ = false;
    disconnect();
    logger_.info("Alpaca market data processing stopped");
}

void AlpacaMarketData::log_status() const {
    const auto& m = metrics_;
    auto now = std::chrono::steady_clock::now();
    auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - m.last_message_time).count();
    
    logger_.info("=== Alpaca Market Data Status ===");
    logger_.info("Connected: " + std::string(connected_.load() ? "Yes" : "No"));
    logger_.info("Running: " + std::string(running_.load() ? "Yes" : "No"));
    logger_.info("Messages received: " + std::to_string(m.messages_received.load()));
    logger_.info("Messages processed: " + std::to_string(m.messages_processed.load()));
    logger_.info("Quotes processed: " + std::to_string(m.quotes_processed.load()));
    logger_.info("Trades processed: " + std::to_string(m.trades_processed.load()));
    logger_.info("Bars processed: " + std::to_string(m.bars_processed.load()));
    logger_.info("Parse errors: " + std::to_string(m.parse_errors.load()));
    logger_.info("Connection errors: " + std::to_string(m.connection_errors.load()));
    logger_.info("Bytes received: " + std::to_string(m.bytes_received.load()));
    logger_.info("Average latency: " + std::to_string(m.get_average_latency_microseconds()) + " μs");
    logger_.info("Time since last message: " + std::to_string(uptime_ms) + " ms");
    logger_.info("===============================");
}

bool AlpacaMarketData::is_healthy() const {
    auto now = std::chrono::steady_clock::now();
    auto time_since_last_msg = std::chrono::duration_cast<std::chrono::seconds>(
        now - metrics_.last_message_time).count();
    
    // Consider healthy if connected and received message within last 60 seconds
    return connected_.load() && running_.load() && time_since_last_msg < 60;
}

// Boost.Beast WebSocket Connection Methods
bool AlpacaMarketData::establish_websocket_connection() {
    logger_.info("Establishing WebSocket connection to Alpaca: " + websocket_url_);
    
    try {
        // Create the websocket stream
        ws_ = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(*ioc_, *ssl_ctx_);
        
        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
            logger_.error("Failed to set SNI hostname");
            return false;
        }
        
        // Look up the domain name
        tcp::resolver resolver(*ioc_);
        auto const results = resolver.resolve(host_, port_);
        
        // Make the connection on the IP address we get from a lookup
        auto ep = beast::get_lowest_layer(*ws_).connect(results);
        
        // Update the host string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        host_ += ":" + std::to_string(ep.port());
        
        // Perform the SSL handshake
        ws_->next_layer().handshake(ssl::stream_base::client);
        
        // Set a decorator to change the User-Agent of the handshake
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "HFT-System/1.0");
            }));
        
        // Perform the websocket handshake
        ws_->handshake(host_, path_);
        
        logger_.info("WebSocket connection established successfully");
        connected_ = true;
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("Failed to establish WebSocket connection: " + std::string(e.what()));
        connected_ = false;
        return false;
    }
}

void AlpacaMarketData::websocket_thread_func() {
    logger_.info("WebSocket thread started");
    logger_.info("Initial state - connected: " + std::string(connected_.load() ? "true" : "false") + ", running: " + std::string(running_.load() ? "true" : "false"));
    
    while (connected_.load() && running_.load()) {
        try {
            logger_.debug("About to read WebSocket message...");
            
            // Read a message into our buffer
            buffer_.clear();
            ws_->read(buffer_);
            
            logger_.debug("Successfully read WebSocket message");
            
            // Convert to string
            std::string message = beast::buffers_to_string(buffer_.data());
            
            metrics_.bytes_received += message.size();
            metrics_.messages_received++;
            metrics_.last_message_time = std::chrono::steady_clock::now();
            
            logger_.debug("Received WebSocket message (" + std::to_string(message.size()) + " bytes): " + message.substr(0, 500));
            
            // Process the message
            process_message(message);
            
        } catch (beast::system_error const& se) {
            logger_.error("Beast system error in WebSocket thread: " + se.code().message());
            logger_.error("Error code: " + std::to_string(se.code().value()) + ", category: " + se.code().category().name());
            
            if (se.code() != websocket::error::closed) {
                logger_.error("Non-close WebSocket error");
                metrics_.connection_errors++;
            } else {
                logger_.info("WebSocket closed normally");
            }
            break;
        } catch (const std::exception& e) {
            logger_.error("General exception in WebSocket thread: " + std::string(e.what()));
            metrics_.connection_errors++;
            break;
        }
    }
    
    logger_.info("WebSocket thread loop exited - connected: " + std::string(connected_.load() ? "true" : "false") + ", running: " + std::string(running_.load() ? "true" : "false"));
    logger_.info("WebSocket thread terminated");
    connected_ = false;
}

bool AlpacaMarketData::send_message(const std::string& message) {
    if (!ws_) {
        logger_.error("WebSocket not initialized (ws_ is null)");
        return false;
    }
    
    if (!connected_.load()) {
        logger_.error("WebSocket not connected (connected_ is false)");
        return false;
    }
    
    try {
        logger_.debug("About to write message to WebSocket");
        ws_->write(net::buffer(message));
        logger_.debug("Successfully sent message: " + message.substr(0, std::min(100UL, message.length())));
        return true;
    } catch (const std::exception& e) {
        logger_.error("Exception while sending message: " + std::string(e.what()));
        connected_ = false; // Mark as disconnected on error
        return false;
    }
}

void AlpacaMarketData::close_websocket_connection() {
    if (ws_ && connected_.load()) {
        try {
            ws_->close(websocket::close_code::normal);
        } catch (const std::exception& e) {
            logger_.debug("Error closing WebSocket: " + std::string(e.what()));
        }
    }
    
    if (ioc_) {
        ioc_->stop();
    }
    
    connected_ = false;
}

// Message Processing - Handle Alpaca's array format
void AlpacaMarketData::process_message(const std::string& message) {
    logger_.debug("Processing message: " + message);
    
    try {
        auto json = nlohmann::json::parse(message);
        
        // Alpaca sends messages as arrays: [{"T": "t", "S": "AAPL", ...}, ...]
        if (json.is_array()) {
            for (const auto& msg : json) {
                process_single_message(msg);
            }
        }
        // Handle single message objects (auth responses, etc.)
        else if (json.is_object()) {
            process_single_message(json);
        } else {
            logger_.warning("Message is neither array nor object: " + message);
        }
        
        metrics_.messages_processed++;
        
    } catch (const std::exception& e) {
        logger_.error("Failed to parse JSON message: " + std::string(e.what()));
        logger_.error("Raw message content: " + message);
        metrics_.parse_errors++;
    }
}

void AlpacaMarketData::process_single_message(const nlohmann::json& msg) {
    if (msg.contains("T")) {
        std::string msg_type = msg["T"];
        logger_.debug("Processing message type: " + msg_type);
        
        std::string msg_str = msg.dump();
        
        if (msg_type == "q") {
            handle_quote_message(msg_str);
        } else if (msg_type == "t") {
            handle_trade_message(msg_str);
        } else if (msg_type == "b" || msg_type == "d" || msg_type == "u") {
            handle_bar_message(msg_str);
        } else {
            logger_.info("Ignoring message type: " + msg_type + ", full message: " + msg_str);
        }
    } else if (msg.contains("msg")) {
        // Handle status/error messages
        std::string status_msg = msg["msg"];
        logger_.info("Received status message: " + status_msg);
        
        if (status_msg == "connected") {
            logger_.info("Alpaca reports connection successful");
        } else if (status_msg == "authenticated") {
            logger_.info("Alpaca authentication confirmed - ready to subscribe");
        } else {
            logger_.warning("Alpaca status: " + status_msg);
        }
    } else {
        logger_.info("Message without 'T' or 'msg' field: " + msg.dump());
    }
}

bool AlpacaMarketData::handle_quote_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        
        std::string symbol = json.value("S", "");
        double bid = json.value("bp", 0.0);
        double ask = json.value("ap", 0.0);
        uint32_t bid_size = json.value("bs", 0u);
        uint32_t ask_size = json.value("as", 0u);
        
        if (symbol.empty() || bid <= 0.0 || ask <= 0.0) {
            return false;
        }
        
        metrics_.quotes_processed++;
        
        // Update last quotes for trade processing
        last_quotes_[symbol] = (bid + ask) / 2.0;
        
        // Convert to internal format and call callback
        if (data_callback_) {
            MarketData market_data = convert_alpaca_quote_to_market_data(symbol, bid, ask, bid_size, ask_size);
            data_callback_(market_data);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.debug("Error parsing quote: " + std::string(e.what()));
        return false;
    }
}

bool AlpacaMarketData::handle_trade_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        
        std::string symbol = json.value("S", "");
        double price = json.value("p", 0.0);
        uint32_t size = json.value("s", 0u);
        
        if (symbol.empty() || price <= 0.0 || size == 0) {
            return false;
        }
        
        metrics_.trades_processed++;
        
        // Convert to internal format and call callback
        if (data_callback_) {
            MarketData market_data = convert_alpaca_trade_to_market_data(symbol, price, size);
            data_callback_(market_data);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.debug("Error parsing trade: " + std::string(e.what()));
        return false;
    }
}

bool AlpacaMarketData::handle_bar_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        
        std::string symbol = json.value("S", "");
        double close = json.value("c", 0.0);
        uint32_t volume = json.value("v", 0u);
        
        if (symbol.empty() || close <= 0.0) {
            return false;
        }
        
        metrics_.bars_processed++;
        
        // For bars, create a market data message using close price as last trade
        if (data_callback_) {
            MarketData market_data = convert_alpaca_trade_to_market_data(symbol, close, volume);
            data_callback_(market_data);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.debug("Error parsing bar: " + std::string(e.what()));
        return false;
    }
}

bool AlpacaMarketData::handle_error_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        std::string error = json.value("msg", "Unknown error");
        logger_.error("Alpaca API error: " + error);
        return true;
    } catch (const std::exception& e) {
        logger_.warning("Error parsing error message: " + std::string(e.what()));
        return false;
    }
}

// Utility Methods
std::string AlpacaMarketData::create_auth_message() {
    nlohmann::json auth;
    auth["action"] = "auth";
    auth["key"] = api_key_;
    auth["secret"] = api_secret_;
    return auth.dump();
}

std::string AlpacaMarketData::create_subscription_message(const std::vector<std::string>& symbols) {
    nlohmann::json sub;
    sub["action"] = "subscribe";
    sub["trades"] = symbols;  // Subscribe to trade data
    sub["quotes"] = symbols;  // Subscribe to quote data 
    sub["bars"] = symbols;    // Subscribe to bar data
    return sub.dump();
}

MarketData AlpacaMarketData::convert_alpaca_quote_to_market_data(const std::string& symbol, double bid, double ask, uint32_t bid_size, uint32_t ask_size) {
    // Use last trade price if available, otherwise use mid-price
    double last_price = (last_quotes_.count(symbol)) ? last_quotes_[symbol] : (bid + ask) / 2.0;
    uint32_t last_size = 100; // Default size for quotes
    
    return MessageFactory::create_market_data(symbol, bid, ask, bid_size, ask_size, last_price, last_size);
}

MarketData AlpacaMarketData::convert_alpaca_trade_to_market_data(const std::string& symbol, double price, uint32_t size) {
    // For trades, use the trade price/size as last, and create synthetic bid/ask
    double spread = price * 0.001; // 0.1% spread assumption
    double bid = price - spread / 2.0;
    double ask = price + spread / 2.0;
    
    return MessageFactory::create_market_data(symbol, bid, ask, size, size, price, size);
}

// Fast JSON parsing helpers (optimized for performance)
std::string AlpacaMarketData::extract_json_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t start = json.find(search);
    if (start == std::string::npos) return "";
    
    start += search.length();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";
    
    return json.substr(start, end - start);
}

double AlpacaMarketData::extract_json_double(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return 0.0;
    
    start += search.length();
    size_t end = json.find_first_of(",}", start);
    if (end == std::string::npos) return 0.0;
    
    try {
        return std::stod(json.substr(start, end - start));
    } catch (...) {
        return 0.0;
    }
}

uint32_t AlpacaMarketData::extract_json_uint(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return 0;
    
    start += search.length();
    size_t end = json.find_first_of(",}", start);
    if (end == std::string::npos) return 0;
    
    try {
        return static_cast<uint32_t>(std::stoul(json.substr(start, end - start)));
    } catch (...) {
        return 0;
    }
}

// Metrics helpers
void AlpacaMarketData::record_latency(std::chrono::steady_clock::time_point start_time) {
    auto end_time = std::chrono::steady_clock::now();
    auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    metrics_.total_latency_microseconds += latency_us;
    metrics_.latency_samples++;
}

void AlpacaMarketData::update_global_metrics() {
    // For now, we'll just log the metrics instead of updating a global registry
    // This can be enhanced later with proper metrics integration
    logger_.info("Alpaca metrics - Messages: " + std::to_string(metrics_.messages_received.load()) +
                 ", Processed: " + std::to_string(metrics_.messages_processed.load()) +
                 ", Errors: " + std::to_string(metrics_.parse_errors.load()) +
                 ", Latency: " + std::to_string(metrics_.get_average_latency_microseconds()) + "μs");
}

} // namespace hft