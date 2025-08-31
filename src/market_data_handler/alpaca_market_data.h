#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <condition_variable>

// Forward declarations for low-latency networking
struct epoll_event;
struct io_uring;
struct io_uring_cqe;

namespace hft {

// Alpaca WebSocket message types
enum class AlpacaMessageType {
    AUTHENTICATION,
    SUBSCRIPTION,
    QUOTE,
    TRADE,
    BAR,
    ERROR,
    HEARTBEAT
};

// Alpaca market data subscription
struct AlpacaSubscription {
    std::vector<std::string> quotes;   // Stock quotes (bid/ask)
    std::vector<std::string> trades;   // Trade executions
    std::vector<std::string> bars;     // OHLCV bars
    std::string feed = "iex";          // "iex" or "sip" (paid)
};

// Low-latency message queue for WebSocket data
template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue(size_t capacity = 65536);
    ~LockFreeQueue();
    
    bool enqueue(const T& item);
    bool dequeue(T& item);
    size_t size() const;
    bool empty() const;

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::unique_ptr<T[]> buffer_;
    size_t capacity_;
    size_t mask_;
};

class AlpacaMarketData {
public:
    AlpacaMarketData();
    ~AlpacaMarketData();
    
    // Initialize with Alpaca credentials
    bool initialize(const std::string& api_key, const std::string& api_secret, bool paper_trading = true);
    
    // Connection management
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_; }
    
    // Subscription management
    bool subscribe(const AlpacaSubscription& subscription);
    bool unsubscribe(const std::vector<std::string>& symbols);
    
    // Set callback for market data
    void set_data_callback(std::function<void(const MarketData&)> callback);
    
    // Start/stop processing
    void start_processing();
    void stop_processing();
    
    // Statistics
    uint64_t get_messages_received() const { return messages_received_; }
    uint64_t get_messages_processed() const { return messages_processed_; }
    uint64_t get_connection_errors() const { return connection_errors_; }
    uint64_t get_parse_errors() const { return parse_errors_; }
    
    // Performance optimization settings
    void set_use_io_uring(bool enable) { use_io_uring_ = enable; }
    void set_cpu_affinity(int cpu_core) { cpu_affinity_ = cpu_core; }
    void set_batch_size(size_t batch_size) { batch_size_ = batch_size; }

private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;
    std::string websocket_url_;
    bool paper_trading_ = true;
    
    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> authenticated_{false};
    std::atomic<bool> processing_{false};
    
    // Statistics
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> messages_processed_{0};
    std::atomic<uint64_t> connection_errors_{0};
    std::atomic<uint64_t> parse_errors_{0};
    
    // Callback
    std::function<void(const MarketData&)> data_callback_;
    
    // Performance optimization settings
    bool use_io_uring_ = false;
    int cpu_affinity_ = -1;
    size_t batch_size_ = 32;
    
    // Networking
    int websocket_fd_ = -1;
    int epoll_fd_ = -1;
    std::unique_ptr<char[]> receive_buffer_;
    static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB receive buffer
    
    // Threading
    std::unique_ptr<std::thread> network_thread_;
    std::unique_ptr<std::thread> processing_thread_;
    std::atomic<bool> should_stop_{false};
    
    // Lock-free message queue
    std::unique_ptr<LockFreeQueue<std::string>> message_queue_;
    
    // Message parsing state
    std::string partial_message_;
    std::unordered_map<std::string, double> last_quotes_;
    
    Logger logger_;
    
    // Core methods
    bool establish_websocket_connection();
    bool authenticate();
    bool send_subscription(const AlpacaSubscription& subscription);
    
    // Network processing (with io_uring optimization)
    void network_loop();
    void network_loop_epoll();
#ifdef HAVE_IO_URING
    void network_loop_io_uring();
    bool setup_io_uring();
    void cleanup_io_uring();
    std::unique_ptr<io_uring> ring_;
#endif
    
    // Message processing
    void processing_loop();
    bool process_websocket_frame(const char* data, size_t length);
    void process_message_batch();
    
    // Protocol handlers
    bool parse_alpaca_message(const std::string& message);
    bool handle_quote_message(const std::string& message);
    bool handle_trade_message(const std::string& message);
    bool handle_bar_message(const std::string& message);
    bool handle_error_message(const std::string& message);
    
    // WebSocket protocol
    std::string create_websocket_key();
    bool validate_websocket_response(const std::string& response);
    std::string create_websocket_frame(const std::string& payload);
    std::string decode_websocket_frame(const char* frame, size_t length, size_t& payload_length);
    
    // Utilities
    std::string create_auth_message();
    std::string create_subscription_message(const AlpacaSubscription& subscription);
    MarketData convert_alpaca_quote_to_market_data(const std::string& symbol, double bid, double ask, 
                                                   uint32_t bid_size, uint32_t ask_size);
    MarketData convert_alpaca_trade_to_market_data(const std::string& symbol, double price, uint32_t size);
    
    // Error handling and reconnection
    void handle_connection_error();
    bool attempt_reconnection();
    void reset_connection_state();
    
    // Performance optimizations
    void set_thread_affinity();
    void set_socket_options();
    void prefault_memory();
    
    // JSON parsing helpers (fast, minimal allocations)
    std::string extract_json_string(const std::string& json, const std::string& key);
    double extract_json_double(const std::string& json, const std::string& key);
    uint32_t extract_json_uint(const std::string& json, const std::string& key);
};

// Template implementation for LockFreeQueue
template<typename T>
LockFreeQueue<T>::LockFreeQueue(size_t capacity) 
    : capacity_(capacity), mask_(capacity - 1) {
    // Ensure capacity is power of 2
    if ((capacity & (capacity - 1)) != 0) {
        throw std::invalid_argument("Capacity must be a power of 2");
    }
    buffer_ = std::make_unique<T[]>(capacity);
}

template<typename T>
LockFreeQueue<T>::~LockFreeQueue() = default;

template<typename T>
bool LockFreeQueue<T>::enqueue(const T& item) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t next_tail = (tail + 1) & mask_;
    
    if (next_tail == head_.load(std::memory_order_acquire)) {
        return false; // Queue is full
    }
    
    buffer_[tail] = item;
    tail_.store(next_tail, std::memory_order_release);
    return true;
}

template<typename T>
bool LockFreeQueue<T>::dequeue(T& item) {
    size_t head = head_.load(std::memory_order_relaxed);
    
    if (head == tail_.load(std::memory_order_acquire)) {
        return false; // Queue is empty
    }
    
    item = buffer_[head];
    head_.store((head + 1) & mask_, std::memory_order_release);
    return true;
}

template<typename T>
size_t LockFreeQueue<T>::size() const {
    return (tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire)) & mask_;
}

template<typename T>
bool LockFreeQueue<T>::empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
}

} // namespace hft