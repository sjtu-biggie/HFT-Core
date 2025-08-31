#include "alpaca_market_data.h"
#include "../common/static_config.h"
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <sched.h>
#include <sys/mman.h>
#include <random>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <chrono>

#ifdef HAVE_IO_URING
#include <liburing.h>
#endif

namespace hft {

AlpacaMarketData::AlpacaMarketData()
    : logger_("AlpacaMarketData", StaticConfig::get_logger_endpoint())
    , receive_buffer_(std::make_unique<char[]>(BUFFER_SIZE))
    , message_queue_(std::make_unique<LockFreeQueue<std::string>>(65536)) {
    
    logger_.info("AlpacaMarketData client initialized");
}

AlpacaMarketData::~AlpacaMarketData() {
    disconnect();
}

bool AlpacaMarketData::initialize(const std::string& api_key, const std::string& api_secret, bool paper_trading) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    paper_trading_ = paper_trading;
    
    // Set Alpaca URLs based on paper/live trading
    if (paper_trading_) {
        base_url_ = "https://paper-api.alpaca.markets";
        websocket_url_ = "wss://stream.data.alpaca.markets/v2/iex";  // Free IEX feed
    } else {
        base_url_ = "https://api.alpaca.markets";
        websocket_url_ = "wss://stream.data.alpaca.markets/v2/sip";  // Paid SIP feed
    }
    
    logger_.info("Alpaca client initialized - Mode: " + std::string(paper_trading_ ? "Paper" : "Live"));
    logger_.info("WebSocket URL: " + websocket_url_);
    
    // Pre-fault memory for better performance
    prefault_memory();
    
    return true;
}

bool AlpacaMarketData::connect() {
    if (connected_.load()) {
        logger_.warning("Already connected to Alpaca WebSocket");
        return true;
    }
    
    logger_.info("Connecting to Alpaca market data WebSocket...");
    
    if (!establish_websocket_connection()) {
        logger_.error("Failed to establish WebSocket connection");
        return false;
    }
    
    if (!authenticate()) {
        logger_.error("Failed to authenticate with Alpaca");
        disconnect();
        return false;
    }
    
    connected_ = true;
    authenticated_ = true;
    logger_.info("Successfully connected to Alpaca market data");
    
    return true;
}

void AlpacaMarketData::disconnect() {
    if (!connected_.load()) return;
    
    logger_.info("Disconnecting from Alpaca WebSocket");
    
    should_stop_ = true;
    connected_ = false;
    authenticated_ = false;
    processing_ = false;
    
    // Stop threads
    if (network_thread_ && network_thread_->joinable()) {
        network_thread_->join();
    }
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    // Close sockets
    if (websocket_fd_ >= 0) {
        close(websocket_fd_);
        websocket_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    
#ifdef HAVE_IO_URING
    cleanup_io_uring();
#endif
    
    logger_.info("Disconnected from Alpaca WebSocket");
}

bool AlpacaMarketData::subscribe(const AlpacaSubscription& subscription) {
    if (!connected_.load()) {
        logger_.error("Not connected to Alpaca WebSocket");
        return false;
    }
    
    return send_subscription(subscription);
}

void AlpacaMarketData::set_data_callback(std::function<void(const MarketData&)> callback) {
    data_callback_ = callback;
}

void AlpacaMarketData::start_processing() {
    if (processing_.load()) {
        logger_.warning("Processing already started");
        return;
    }
    
    if (!connected_.load()) {
        logger_.error("Cannot start processing - not connected");
        return;
    }
    
    processing_ = true;
    should_stop_ = false;
    
    // Start network and processing threads
#ifdef HAVE_IO_URING
    if (use_io_uring_) {
        network_thread_ = std::make_unique<std::thread>(&AlpacaMarketData::network_loop_io_uring, this);
    } else {
        network_thread_ = std::make_unique<std::thread>(&AlpacaMarketData::network_loop_epoll, this);
    }
#else
    network_thread_ = std::make_unique<std::thread>(&AlpacaMarketData::network_loop_epoll, this);
#endif
    
    processing_thread_ = std::make_unique<std::thread>(&AlpacaMarketData::processing_loop, this);
    
    logger_.info("Alpaca market data processing started");
}

void AlpacaMarketData::stop_processing() {
    if (!processing_.load()) return;
    
    logger_.info("Stopping Alpaca market data processing");
    should_stop_ = true;
    processing_ = false;
    
    // Threads will be joined in disconnect()
}

bool AlpacaMarketData::establish_websocket_connection() {
    // Parse WebSocket URL
    std::string host, path;
    int port = 443; // Default HTTPS/WSS port
    
    size_t protocol_end = websocket_url_.find("://");
    if (protocol_end == std::string::npos) {
        logger_.error("Invalid WebSocket URL: " + websocket_url_);
        return false;
    }
    
    size_t host_start = protocol_end + 3;
    size_t path_start = websocket_url_.find('/', host_start);
    
    if (path_start == std::string::npos) {
        host = websocket_url_.substr(host_start);
        path = "/";
    } else {
        host = websocket_url_.substr(host_start, path_start - host_start);
        path = websocket_url_.substr(path_start);
    }
    
    // Create socket
    websocket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (websocket_fd_ < 0) {
        logger_.error("Failed to create socket: " + std::string(strerror(errno)));
        return false;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(websocket_fd_, F_GETFL, 0);
    fcntl(websocket_fd_, F_SETFL, flags | O_NONBLOCK);
    
    // Set socket options for low latency
    set_socket_options();
    
    // Resolve hostname (simplified - would use getaddrinfo in production)
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    // For demo purposes, hardcode Alpaca's IP (would resolve DNS in production)
    if (inet_pton(AF_INET, "54.230.180.82", &server_addr.sin_addr) <= 0) {
        logger_.error("Invalid server address");
        close(websocket_fd_);
        websocket_fd_ = -1;
        return false;
    }
    
    // Connect
    if (::connect(websocket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            logger_.error("Connection failed: " + std::string(strerror(errno)));
            close(websocket_fd_);
            websocket_fd_ = -1;
            return false;
        }
    }
    
    // Wait for connection to complete
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(websocket_fd_, &write_fds);
    
    struct timeval timeout = {10, 0}; // 10 second timeout
    if (select(websocket_fd_ + 1, nullptr, &write_fds, nullptr, &timeout) <= 0) {
        logger_.error("Connection timeout");
        close(websocket_fd_);
        websocket_fd_ = -1;
        return false;
    }
    
    // Check if connection was successful
    int error;
    socklen_t len = sizeof(error);
    if (getsockopt(websocket_fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        logger_.error("Connection failed with error: " + std::string(strerror(error)));
        close(websocket_fd_);
        websocket_fd_ = -1;
        return false;
    }
    
    // Send WebSocket handshake
    std::string websocket_key = create_websocket_key();
    std::string handshake = 
        "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + websocket_key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: alpaca-data\r\n"
        "\r\n";
    
    if (send(websocket_fd_, handshake.c_str(), handshake.length(), 0) < 0) {
        logger_.error("Failed to send WebSocket handshake: " + std::string(strerror(errno)));
        close(websocket_fd_);
        websocket_fd_ = -1;
        return false;
    }
    
    // Read handshake response
    char response_buffer[4096];
    ssize_t bytes_received = recv(websocket_fd_, response_buffer, sizeof(response_buffer) - 1, 0);
    if (bytes_received <= 0) {
        logger_.error("Failed to receive WebSocket handshake response");
        close(websocket_fd_);
        websocket_fd_ = -1;
        return false;
    }
    
    response_buffer[bytes_received] = '\0';
    std::string response(response_buffer);
    
    if (!validate_websocket_response(response)) {
        logger_.error("Invalid WebSocket handshake response");
        close(websocket_fd_);
        websocket_fd_ = -1;
        return false;
    }
    
    logger_.info("WebSocket connection established successfully");
    return true;
}

bool AlpacaMarketData::authenticate() {
    std::string auth_message = create_auth_message();
    std::string frame = create_websocket_frame(auth_message);
    
    if (send(websocket_fd_, frame.c_str(), frame.length(), 0) < 0) {
        logger_.error("Failed to send authentication message: " + std::string(strerror(errno)));
        return false;
    }
    
    // Wait for authentication response (simplified - should parse WebSocket frames)
    char response[1024];
    ssize_t bytes_received = recv(websocket_fd_, response, sizeof(response) - 1, 0);
    if (bytes_received <= 0) {
        logger_.error("Failed to receive authentication response");
        return false;
    }
    
    response[bytes_received] = '\0';
    std::string response_str(response);
    
    // Simple check for successful authentication (would parse properly in production)
    if (response_str.find("connected") != std::string::npos) {
        logger_.info("Alpaca authentication successful");
        return true;
    } else {
        logger_.error("Alpaca authentication failed");
        return false;
    }
}

bool AlpacaMarketData::send_subscription(const AlpacaSubscription& subscription) {
    std::string sub_message = create_subscription_message(subscription);
    std::string frame = create_websocket_frame(sub_message);
    
    if (send(websocket_fd_, frame.c_str(), frame.length(), 0) < 0) {
        logger_.error("Failed to send subscription message: " + std::string(strerror(errno)));
        return false;
    }
    
    logger_.info("Sent subscription for " + std::to_string(subscription.quotes.size()) + " symbols");
    return true;
}

void AlpacaMarketData::network_loop_epoll() {
    logger_.info("Starting network loop with epoll");
    
    // Set thread affinity if specified
    set_thread_affinity();
    
    // Create epoll instance
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        logger_.error("Failed to create epoll instance: " + std::string(strerror(errno)));
        return;
    }
    
    // Add WebSocket fd to epoll
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET; // Edge-triggered for best performance
    event.data.fd = websocket_fd_;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, websocket_fd_, &event) < 0) {
        logger_.error("Failed to add WebSocket fd to epoll: " + std::string(strerror(errno)));
        return;
    }
    
    const int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];
    
    while (!should_stop_.load()) {
        int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100); // 100ms timeout
        
        if (num_events < 0) {
            if (errno == EINTR) continue;
            logger_.error("epoll_wait failed: " + std::string(strerror(errno)));
            break;
        }
        
        for (int i = 0; i < num_events; ++i) {
            if (events[i].data.fd == websocket_fd_) {
                // Read data from WebSocket
                ssize_t bytes_received = recv(websocket_fd_, receive_buffer_.get(), BUFFER_SIZE - 1, 0);
                
                if (bytes_received > 0) {
                    messages_received_++;
                    receive_buffer_[bytes_received] = '\0';
                    
                    // Process WebSocket frames and extract messages
                    if (process_websocket_frame(receive_buffer_.get(), bytes_received)) {
                        // Frame processed successfully
                    }
                    
                } else if (bytes_received == 0) {
                    logger_.warning("WebSocket connection closed by server");
                    handle_connection_error();
                    break;
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        logger_.error("recv failed: " + std::string(strerror(errno)));
                        handle_connection_error();
                        break;
                    }
                }
            }
        }
    }
    
    logger_.info("Network loop terminated");
}

void AlpacaMarketData::processing_loop() {
    logger_.info("Starting message processing loop");
    
    // Set thread affinity if specified  
    set_thread_affinity();
    
    while (!should_stop_.load()) {
        process_message_batch();
        
        // Small sleep to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    logger_.info("Processing loop terminated");
}

bool AlpacaMarketData::process_websocket_frame(const char* data, size_t length) {
    // Simplified WebSocket frame parsing - would need full implementation for production
    // For now, assume we receive JSON messages directly
    
    std::string message(data, length);
    
    // Handle potential partial messages
    partial_message_ += message;
    
    // Look for complete JSON messages (ending with '}')
    size_t pos = 0;
    while ((pos = partial_message_.find('}', pos)) != std::string::npos) {
        std::string complete_message = partial_message_.substr(0, pos + 1);
        partial_message_ = partial_message_.substr(pos + 1);
        
        // Enqueue message for processing
        if (!message_queue_->enqueue(complete_message)) {
            logger_.warning("Message queue full, dropping message");
            return false;
        }
        
        pos = 0;
    }
    
    return true;
}

void AlpacaMarketData::process_message_batch() {
    std::string message;
    size_t processed_count = 0;
    
    // Process messages in batches for better performance
    while (processed_count < batch_size_ && message_queue_->dequeue(message)) {
        if (parse_alpaca_message(message)) {
            messages_processed_++;
        } else {
            parse_errors_++;
        }
        processed_count++;
    }
}

bool AlpacaMarketData::parse_alpaca_message(const std::string& message) {
    // Fast JSON parsing for Alpaca messages
    // Look for message type indicator
    
    if (message.find("\"T\":\"q\"") != std::string::npos) {
        return handle_quote_message(message);
    } else if (message.find("\"T\":\"t\"") != std::string::npos) {
        return handle_trade_message(message);
    } else if (message.find("\"T\":\"b\"") != std::string::npos) {
        return handle_bar_message(message);
    } else if (message.find("\"error\"") != std::string::npos) {
        return handle_error_message(message);
    }
    
    // Unknown message type
    return false;
}

bool AlpacaMarketData::handle_quote_message(const std::string& message) {
    // Extract quote data using fast JSON parsing
    std::string symbol = extract_json_string(message, "S");
    double bid = extract_json_double(message, "bp");
    double ask = extract_json_double(message, "ap");
    uint32_t bid_size = extract_json_uint(message, "bs");
    uint32_t ask_size = extract_json_uint(message, "as");
    
    if (symbol.empty() || bid <= 0.0 || ask <= 0.0) {
        return false;
    }
    
    // Convert to internal format and call callback
    if (data_callback_) {
        MarketData market_data = convert_alpaca_quote_to_market_data(symbol, bid, ask, bid_size, ask_size);
        
        // Update last quotes for trade processing
        last_quotes_[symbol] = (bid + ask) / 2.0;
        
        data_callback_(market_data);
    }
    
    return true;
}

bool AlpacaMarketData::handle_trade_message(const std::string& message) {
    // Extract trade data
    std::string symbol = extract_json_string(message, "S");
    double price = extract_json_double(message, "p");
    uint32_t size = extract_json_uint(message, "s");
    
    if (symbol.empty() || price <= 0.0 || size == 0) {
        return false;
    }
    
    // Convert to internal format and call callback
    if (data_callback_) {
        MarketData market_data = convert_alpaca_trade_to_market_data(symbol, price, size);
        data_callback_(market_data);
    }
    
    return true;
}

bool AlpacaMarketData::handle_bar_message(const std::string& message) {
    // Extract OHLCV bar data
    std::string symbol = extract_json_string(message, "S");
    double open = extract_json_double(message, "o");
    double high = extract_json_double(message, "h");
    double low = extract_json_double(message, "l");
    double close = extract_json_double(message, "c");
    uint32_t volume = extract_json_uint(message, "v");
    
    if (symbol.empty()) {
        return false;
    }
    
    // For bars, we'll create a market data message using close price as last trade
    if (data_callback_ && close > 0.0) {
        MarketData market_data = convert_alpaca_trade_to_market_data(symbol, close, volume);
        data_callback_(market_data);
    }
    
    return true;
}

bool AlpacaMarketData::handle_error_message(const std::string& message) {
    std::string error = extract_json_string(message, "error");
    logger_.error("Alpaca API error: " + error);
    return true;
}

// Helper methods implementation would continue...

std::string AlpacaMarketData::create_websocket_key() {
    // Generate random 16-byte key and base64 encode it
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    uint8_t key_bytes[16];
    for (int i = 0; i < 16; ++i) {
        key_bytes[i] = dis(gen);
    }
    
    // Base64 encode (simplified implementation)
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    for (int i = 0; i < 16; i += 3) {
        uint32_t val = (key_bytes[i] << 16) | (key_bytes[i+1] << 8) | key_bytes[i+2];
        result += chars[(val >> 18) & 0x3F];
        result += chars[(val >> 12) & 0x3F];
        result += chars[(val >> 6) & 0x3F];
        result += chars[val & 0x3F];
    }
    
    return result;
}

bool AlpacaMarketData::validate_websocket_response(const std::string& response) {
    return response.find("HTTP/1.1 101") != std::string::npos &&
           response.find("Upgrade: websocket") != std::string::npos;
}

std::string AlpacaMarketData::create_websocket_frame(const std::string& payload) {
    // Create WebSocket frame (simplified - text frame)
    std::string frame;
    frame.push_back(0x81); // FIN=1, opcode=1 (text)
    
    if (payload.length() < 126) {
        frame.push_back(static_cast<char>(payload.length()));
    } else if (payload.length() < 65536) {
        frame.push_back(126);
        frame.push_back((payload.length() >> 8) & 0xFF);
        frame.push_back(payload.length() & 0xFF);
    } else {
        // Extended payload length (64-bit) - not implemented for simplicity
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((payload.length() >> (i * 8)) & 0xFF);
        }
    }
    
    frame += payload;
    return frame;
}

std::string AlpacaMarketData::create_auth_message() {
    return "{\"action\":\"auth\",\"key\":\"" + api_key_ + "\",\"secret\":\"" + api_secret_ + "\"}";
}

std::string AlpacaMarketData::create_subscription_message(const AlpacaSubscription& subscription) {
    std::string message = "{\"action\":\"subscribe\",";
    
    if (!subscription.quotes.empty()) {
        message += "\"quotes\":[";
        for (size_t i = 0; i < subscription.quotes.size(); ++i) {
            if (i > 0) message += ",";
            message += "\"" + subscription.quotes[i] + "\"";
        }
        message += "]";
    }
    
    if (!subscription.trades.empty()) {
        if (!subscription.quotes.empty()) message += ",";
        message += "\"trades\":[";
        for (size_t i = 0; i < subscription.trades.size(); ++i) {
            if (i > 0) message += ",";
            message += "\"" + subscription.trades[i] + "\"";
        }
        message += "]";
    }
    
    message += "}";
    return message;
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

void AlpacaMarketData::set_socket_options() {
    if (websocket_fd_ < 0) return;
    
    // Set TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(websocket_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Set SO_RCVBUF for larger receive buffer
    int rcvbuf = BUFFER_SIZE;
    setsockopt(websocket_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Set SO_SNDBUF for larger send buffer
    int sndbuf = 65536;
    setsockopt(websocket_fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
}

void AlpacaMarketData::set_thread_affinity() {
    if (cpu_affinity_ >= 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_affinity_, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }
}

void AlpacaMarketData::prefault_memory() {
    // Touch memory pages to prefault them
    volatile char* buffer = receive_buffer_.get();
    for (size_t i = 0; i < BUFFER_SIZE; i += 4096) {
        buffer[i] = 0;
    }
}

void AlpacaMarketData::handle_connection_error() {
    connection_errors_++;
    logger_.warning("Connection error occurred, attempting reconnection...");
    
    reset_connection_state();
    
    // Attempt reconnection (simplified)
    if (attempt_reconnection()) {
        logger_.info("Reconnection successful");
    } else {
        logger_.error("Reconnection failed");
    }
}

bool AlpacaMarketData::attempt_reconnection() {
    // Simple reconnection logic - would be more sophisticated in production
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return connect();
}

void AlpacaMarketData::reset_connection_state() {
    connected_ = false;
    authenticated_ = false;
    
    if (websocket_fd_ >= 0) {
        close(websocket_fd_);
        websocket_fd_ = -1;
    }
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

} // namespace hft