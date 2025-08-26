#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <atomic>

namespace hft {

// Transport types supported
enum class TransportType {
    ZEROMQ,
    SPMC_RING,
    SHARED_MEMORY
};

// Transport patterns
enum class TransportPattern {
    PUBLISH_SUBSCRIBE,  // One-to-many (market data)
    PUSH_PULL,         // Many-to-one (signal aggregation)
    REQUEST_REPLY,     // RPC style
    PAIR              // Bidirectional
};

// Transport configuration
struct TransportConfig {
    TransportType type;
    TransportPattern pattern;
    std::string endpoint;
    size_t buffer_size = 1024 * 1024;  // 1MB default
    int high_water_mark = 1000;
    bool blocking = false;
    
    TransportConfig(TransportType t, TransportPattern p, const std::string& ep)
        : type(t), pattern(p), endpoint(ep) {}
};

// Abstract message transport interface
class IMessageTransport {
public:
    virtual ~IMessageTransport() = default;
    
    // Connection management
    virtual bool initialize(const TransportConfig& config) = 0;
    virtual bool bind(const std::string& endpoint) = 0;
    virtual bool connect(const std::string& endpoint) = 0;
    virtual void close() = 0;
    
    // Synchronous I/O
    virtual bool send(const void* data, size_t size, bool non_blocking = false) = 0;
    virtual bool receive(void* data, size_t& size, bool non_blocking = false) = 0;
    
    // Asynchronous I/O with callbacks
    using MessageCallback = std::function<void(const void* data, size_t size)>;
    virtual void set_receive_callback(MessageCallback callback) = 0;
    virtual void start_async_receive() = 0;
    virtual void stop_async_receive() = 0;
    
    // Status and configuration
    virtual bool is_connected() const = 0;
    virtual TransportType get_type() const = 0;
    virtual std::string get_endpoint() const = 0;
    
    // Performance metrics
    virtual uint64_t get_messages_sent() const = 0;
    virtual uint64_t get_messages_received() const = 0;
    virtual uint64_t get_bytes_sent() const = 0;
    virtual uint64_t get_bytes_received() const = 0;
    
    // Socket/handle for polling (ZMQ compatibility)
    virtual void* get_native_handle() = 0;
};

// Publisher interface (one-to-many)
class IMessagePublisher : public virtual IMessageTransport {
public:
    virtual bool publish(const void* data, size_t size) = 0;
    
    // Topic-based publishing (for pub/sub)
    virtual bool publish(const std::string& topic, const void* data, size_t size) = 0;
    virtual void set_filter(const std::string& filter) = 0;
};

// Subscriber interface (one-to-many consumer)
class IMessageSubscriber : public virtual IMessageTransport {
public:
    virtual bool subscribe(const std::string& topic = "") = 0;
    virtual bool unsubscribe(const std::string& topic = "") = 0;
};

// Push interface (many-to-one producer)
class IMessagePusher : public virtual IMessageTransport {
public:
    virtual bool push(const void* data, size_t size) = 0;
};

// Pull interface (many-to-one consumer)
class IMessagePuller : public virtual IMessageTransport {
public:
    virtual bool pull(void* data, size_t& size, bool non_blocking = false) = 0;
};

// Factory for creating transport instances
class TransportFactory {
public:
    // Create publisher
    static std::unique_ptr<IMessagePublisher> create_publisher(const TransportConfig& config);
    
    // Create subscriber
    static std::unique_ptr<IMessageSubscriber> create_subscriber(const TransportConfig& config);
    
    // Create pusher
    static std::unique_ptr<IMessagePusher> create_pusher(const TransportConfig& config);
    
    // Create puller
    static std::unique_ptr<IMessagePuller> create_puller(const TransportConfig& config);
    
    // Create generic transport
    static std::unique_ptr<IMessageTransport> create_transport(const TransportConfig& config);
    
    // Get available transport types
    static std::vector<TransportType> get_supported_types();
    
    // Get transport type name
    static std::string get_type_name(TransportType type);
    
    // Parse transport type from string
    static TransportType parse_type(const std::string& type_name);
};

// High-performance ring buffer for SPMC (Single Producer, Multiple Consumer)
template<size_t SIZE = 1024 * 1024>  // 1MB default
class SPMCRingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "Size must be power of 2");
    
public:
    struct Message {
        size_t size;
        alignas(64) char data[];  // Flexible array member (C99)
    };
    
    SPMCRingBuffer();
    ~SPMCRingBuffer();
    
    // Producer interface (single producer)
    bool push(const void* data, size_t size);
    
    // Consumer interface (multiple consumers)
    bool pop(void* data, size_t& size, uint32_t consumer_id);
    
    // Consumer management
    uint32_t register_consumer();
    void unregister_consumer(uint32_t consumer_id);
    
    // Status
    bool empty() const;
    bool full() const;
    size_t size() const;
    size_t capacity() const { return SIZE; }
    
private:
    alignas(64) char buffer_[SIZE];
    alignas(64) std::atomic<uint64_t> write_pos_{0};
    alignas(64) std::atomic<uint64_t> read_positions_[32];  // Support up to 32 consumers
    alignas(64) std::atomic<uint32_t> consumer_count_{0};
    alignas(64) std::atomic<uint32_t> next_consumer_id_{0};
    
    static constexpr size_t MASK = SIZE - 1;
    static constexpr size_t MAX_MESSAGE_SIZE = SIZE / 4;  // Max 25% of buffer per message
};

} // namespace hft