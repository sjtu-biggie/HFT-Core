#include "simple_transport_demo.h"
#include <iostream>
#include <cstring>

namespace hft {

// ZmqSimpleTransport implementation
ZmqSimpleTransport::ZmqSimpleTransport(int socket_type) 
    : socket_type_(socket_type) {
}

ZmqSimpleTransport::~ZmqSimpleTransport() {
    close();
}

bool ZmqSimpleTransport::initialize(const std::string& endpoint) {
    if (initialized_) return true;
    
    endpoint_ = endpoint;
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, socket_type_);
        
        // Configure socket
        int hwm = 1000;
        socket_->set(zmq::sockopt::sndhwm, hwm);
        socket_->set(zmq::sockopt::rcvhwm, hwm);
        socket_->set(zmq::sockopt::linger, 0);
        
        // Subscribe to all messages for SUB sockets
        if (socket_type_ == ZMQ_SUB) {
            socket_->set(zmq::sockopt::subscribe, "");
        }
        
        initialized_ = true;
        std::cout << "[SimpleTransport] Initialized ZMQ transport: " << endpoint << std::endl;
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[SimpleTransport] Initialize error: " << e.what() << std::endl;
        return false;
    }
}

bool ZmqSimpleTransport::bind() {
    if (!initialized_) return false;
    
    try {
        socket_->bind(endpoint_);
        std::cout << "[SimpleTransport] Bound to: " << endpoint_ << std::endl;
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[SimpleTransport] Bind error: " << e.what() << std::endl;
        return false;
    }
}

bool ZmqSimpleTransport::connect() {
    if (!initialized_) return false;
    
    try {
        socket_->connect(endpoint_);
        std::cout << "[SimpleTransport] Connected to: " << endpoint_ << std::endl;
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[SimpleTransport] Connect error: " << e.what() << std::endl;
        return false;
    }
}

void ZmqSimpleTransport::close() {
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    context_.reset();
    initialized_ = false;
}

bool ZmqSimpleTransport::send(const void* data, size_t size) {
    if (!socket_) return false;
    
    try {
        zmq::message_t message(size);
        std::memcpy(message.data(), data, size);
        return socket_->send(message, zmq::send_flags::dontwait).has_value();
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) {
            std::cerr << "[SimpleTransport] Send error: " << e.what() << std::endl;
        }
        return false;
    }
}

bool ZmqSimpleTransport::receive(void* data, size_t& size, bool non_blocking) {
    if (!socket_) return false;
    
    try {
        zmq::message_t message;
        auto flags = non_blocking ? zmq::recv_flags::dontwait : zmq::recv_flags::none;
        
        if (socket_->recv(message, flags)) {
            if (message.size() <= size) {
                std::memcpy(data, message.data(), message.size());
                size = message.size();
                return true;
            } else {
                std::cerr << "[SimpleTransport] Receive buffer too small" << std::endl;
                return false;
            }
        }
        return false;
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN || !non_blocking) {
            std::cerr << "[SimpleTransport] Receive error: " << e.what() << std::endl;
        }
        return false;
    }
}

// SimpleTransportFactory implementation
std::unique_ptr<ISimpleTransport> SimpleTransportFactory::create_publisher(SimpleTransportType type) {
    switch (type) {
        case SimpleTransportType::ZEROMQ:
            return std::make_unique<ZmqSimpleTransport>(ZMQ_PUB);
        case SimpleTransportType::SPMC_RING:
            std::cout << "[SimpleTransport] SPMC transport not yet implemented, falling back to ZMQ" << std::endl;
            return std::make_unique<ZmqSimpleTransport>(ZMQ_PUB);
        default:
            return nullptr;
    }
}

std::unique_ptr<ISimpleTransport> SimpleTransportFactory::create_subscriber(SimpleTransportType type) {
    switch (type) {
        case SimpleTransportType::ZEROMQ:
            return std::make_unique<ZmqSimpleTransport>(ZMQ_SUB);
        case SimpleTransportType::SPMC_RING:
            std::cout << "[SimpleTransport] SPMC transport not yet implemented, falling back to ZMQ" << std::endl;
            return std::make_unique<ZmqSimpleTransport>(ZMQ_SUB);
        default:
            return nullptr;
    }
}

std::unique_ptr<ISimpleTransport> SimpleTransportFactory::create_pusher(SimpleTransportType type) {
    switch (type) {
        case SimpleTransportType::ZEROMQ:
            return std::make_unique<ZmqSimpleTransport>(ZMQ_PUSH);
        case SimpleTransportType::SPMC_RING:
            std::cout << "[SimpleTransport] SPMC transport not yet implemented, falling back to ZMQ" << std::endl;
            return std::make_unique<ZmqSimpleTransport>(ZMQ_PUSH);
        default:
            return nullptr;
    }
}

std::unique_ptr<ISimpleTransport> SimpleTransportFactory::create_puller(SimpleTransportType type) {
    switch (type) {
        case SimpleTransportType::ZEROMQ:
            return std::make_unique<ZmqSimpleTransport>(ZMQ_PULL);
        case SimpleTransportType::SPMC_RING:
            std::cout << "[SimpleTransport] SPMC transport not yet implemented, falling back to ZMQ" << std::endl;
            return std::make_unique<ZmqSimpleTransport>(ZMQ_PULL);
        default:
            return nullptr;
    }
}

std::string SimpleTransportFactory::get_type_name(SimpleTransportType type) {
    switch (type) {
        case SimpleTransportType::ZEROMQ: return "zeromq";
        case SimpleTransportType::SPMC_RING: return "spmc";
        default: return "unknown";
    }
}

SimpleTransportType SimpleTransportFactory::parse_type_from_config(const std::string& config_value) {
    if (config_value == "zeromq" || config_value == "zmq") {
        return SimpleTransportType::ZEROMQ;
    } else if (config_value == "spmc" || config_value == "ring") {
        return SimpleTransportType::SPMC_RING;
    } else {
        std::cout << "[SimpleTransport] Unknown transport type '" << config_value 
                  << "', defaulting to ZeroMQ" << std::endl;
        return SimpleTransportType::ZEROMQ;
    }
}

} // namespace hft