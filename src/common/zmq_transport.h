#pragma once

#include "transport_interface.h"
#include <zmq.hpp>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

namespace hft {

// Base ZeroMQ transport implementation
class ZmqTransportBase : public IMessageTransport {
public:
    ZmqTransportBase(int socket_type);
    virtual ~ZmqTransportBase();
    
    // IMessageTransport interface
    bool initialize(const TransportConfig& config) override;
    bool bind(const std::string& endpoint) override;
    bool connect(const std::string& endpoint) override;
    void close() override;
    
    bool send(const void* data, size_t size, bool non_blocking = false) override;
    bool receive(void* data, size_t& size, bool non_blocking = false) override;
    
    void set_receive_callback(MessageCallback callback) override;
    void start_async_receive() override;
    void stop_async_receive() override;
    
    bool is_connected() const override { return connected_.load(); }
    TransportType get_type() const override { return TransportType::ZEROMQ; }
    std::string get_endpoint() const override { return endpoint_; }
    
    uint64_t get_messages_sent() const override { return messages_sent_.load(); }
    uint64_t get_messages_received() const override { return messages_received_.load(); }
    uint64_t get_bytes_sent() const override { return bytes_sent_.load(); }
    uint64_t get_bytes_received() const override { return bytes_received_.load(); }
    
    void* get_native_handle() override;

protected:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    int socket_type_;
    std::string endpoint_;
    TransportConfig config_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> initialized_{false};
    
    // Statistics
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};
    
    // Async receive
    MessageCallback receive_callback_;
    std::unique_ptr<std::thread> receive_thread_;
    std::atomic<bool> async_active_{false};
    mutable std::mutex mutex_;
    
    void async_receive_loop();
    bool configure_socket();
};

// ZeroMQ Publisher implementation
class ZmqPublisher : public ZmqTransportBase, public virtual IMessagePublisher {
public:
    ZmqPublisher() : ZmqTransportBase(ZMQ_PUB) {}
    
    // IMessagePublisher interface
    bool publish(const void* data, size_t size) override;
    bool publish(const std::string& topic, const void* data, size_t size) override;
    void set_filter(const std::string& filter) override;
};

// ZeroMQ Subscriber implementation  
class ZmqSubscriber : public ZmqTransportBase, public virtual IMessageSubscriber {
public:
    ZmqSubscriber() : ZmqTransportBase(ZMQ_SUB) {}
    
    // IMessageSubscriber interface
    bool subscribe(const std::string& topic = "") override;
    bool unsubscribe(const std::string& topic = "") override;
};

// ZeroMQ Pusher implementation
class ZmqPusher : public ZmqTransportBase, public virtual IMessagePusher {
public:
    ZmqPusher() : ZmqTransportBase(ZMQ_PUSH) {}
    
    // IMessagePusher interface
    bool push(const void* data, size_t size) override;
};

// ZeroMQ Puller implementation
class ZmqPuller : public ZmqTransportBase, public virtual IMessagePuller {
public:
    ZmqPuller() : ZmqTransportBase(ZMQ_PULL) {}
    
    // IMessagePuller interface
    bool pull(void* data, size_t& size, bool non_blocking = false) override;
};

} // namespace hft