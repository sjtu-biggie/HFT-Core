#pragma once

#include <string>
#include <memory>
#include <zmq.hpp>

namespace hft {

// Simple transport abstraction demonstration
enum class SimpleTransportType {
    ZEROMQ,
    SPMC_RING  // Placeholder for future SPMC implementation
};

// Simple transport interface
class ISimpleTransport {
public:
    virtual ~ISimpleTransport() = default;
    
    virtual bool initialize(const std::string& endpoint) = 0;
    virtual bool bind() = 0;
    virtual bool connect() = 0;
    virtual void close() = 0;
    
    virtual bool send(const void* data, size_t size) = 0;
    virtual bool receive(void* data, size_t& size, bool non_blocking = false) = 0;
    
    virtual SimpleTransportType get_type() const = 0;
    virtual std::string get_endpoint() const = 0;
};

// ZeroMQ implementation
class ZmqSimpleTransport : public ISimpleTransport {
public:
    explicit ZmqSimpleTransport(int socket_type);
    ~ZmqSimpleTransport() override;
    
    bool initialize(const std::string& endpoint) override;
    bool bind() override;
    bool connect() override;
    void close() override;
    
    bool send(const void* data, size_t size) override;
    bool receive(void* data, size_t& size, bool non_blocking = false) override;
    
    SimpleTransportType get_type() const override { return SimpleTransportType::ZEROMQ; }
    std::string get_endpoint() const override { return endpoint_; }

private:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    std::string endpoint_;
    int socket_type_;
    bool initialized_ = false;
};

// Factory for creating transports
class SimpleTransportFactory {
public:
    // Create publisher (PUB socket)
    static std::unique_ptr<ISimpleTransport> create_publisher(SimpleTransportType type = SimpleTransportType::ZEROMQ);
    
    // Create subscriber (SUB socket) 
    static std::unique_ptr<ISimpleTransport> create_subscriber(SimpleTransportType type = SimpleTransportType::ZEROMQ);
    
    // Create pusher (PUSH socket)
    static std::unique_ptr<ISimpleTransport> create_pusher(SimpleTransportType type = SimpleTransportType::ZEROMQ);
    
    // Create puller (PULL socket)
    static std::unique_ptr<ISimpleTransport> create_puller(SimpleTransportType type = SimpleTransportType::ZEROMQ);
    
    // Get type name
    static std::string get_type_name(SimpleTransportType type);
    
    // Parse type from config
    static SimpleTransportType parse_type_from_config(const std::string& config_value);
};

} // namespace hft