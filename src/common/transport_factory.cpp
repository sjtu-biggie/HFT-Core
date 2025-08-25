#include "transport_interface.h"
#include "zmq_transport.h"
#include "spmc_transport.h"
#include <stdexcept>
#include <algorithm>

namespace hft {

std::unique_ptr<IMessagePublisher> TransportFactory::create_publisher(const TransportConfig& config) {
    switch (config.type) {
        case TransportType::ZEROMQ:
            return std::make_unique<ZmqPublisher>();
            
        case TransportType::SPMC_RING:
            if (config.buffer_size <= 1024 * 1024) {
                return std::make_unique<SPMCPublisher<1024 * 1024>>();
            } else if (config.buffer_size <= 4 * 1024 * 1024) {
                return std::make_unique<SPMCPublisher<4 * 1024 * 1024>>();
            } else {
                return std::make_unique<SPMCPublisher<16 * 1024 * 1024>>();
            }
            
        default:
            throw std::runtime_error("Unsupported transport type for publisher");
    }
}

std::unique_ptr<IMessageSubscriber> TransportFactory::create_subscriber(const TransportConfig& config) {
    switch (config.type) {
        case TransportType::ZEROMQ:
            return std::make_unique<ZmqSubscriber>();
            
        case TransportType::SPMC_RING:
            if (config.buffer_size <= 1024 * 1024) {
                return std::make_unique<SPMCSubscriber<1024 * 1024>>();
            } else if (config.buffer_size <= 4 * 1024 * 1024) {
                return std::make_unique<SPMCSubscriber<4 * 1024 * 1024>>();
            } else {
                return std::make_unique<SPMCSubscriber<16 * 1024 * 1024>>();
            }
            
        default:
            throw std::runtime_error("Unsupported transport type for subscriber");
    }
}

std::unique_ptr<IMessagePusher> TransportFactory::create_pusher(const TransportConfig& config) {
    switch (config.type) {
        case TransportType::ZEROMQ:
            return std::make_unique<ZmqPusher>();
            
        default:
            throw std::runtime_error("Unsupported transport type for pusher");
    }
}

std::unique_ptr<IMessagePuller> TransportFactory::create_puller(const TransportConfig& config) {
    switch (config.type) {
        case TransportType::ZEROMQ:
            return std::make_unique<ZmqPuller>();
            
        default:
            throw std::runtime_error("Unsupported transport type for puller");
    }
}

std::unique_ptr<IMessageTransport> TransportFactory::create_transport(const TransportConfig& config) {
    switch (config.pattern) {
        case TransportPattern::PUBLISH_SUBSCRIBE:
            // Return subscriber by default for pub/sub pattern
            return create_subscriber(config);
            
        case TransportPattern::PUSH_PULL:
            // Return puller by default for push/pull pattern
            return create_puller(config);
            
        default:
            throw std::runtime_error("Unsupported transport pattern");
    }
}

std::vector<TransportType> TransportFactory::get_supported_types() {
    return {
        TransportType::ZEROMQ,
        TransportType::SPMC_RING
    };
}

std::string TransportFactory::get_type_name(TransportType type) {
    switch (type) {
        case TransportType::ZEROMQ: return "zeromq";
        case TransportType::SPMC_RING: return "spmc";
        case TransportType::SHARED_MEMORY: return "shmem";
        default: return "unknown";
    }
}

TransportType TransportFactory::parse_type(const std::string& type_name) {
    std::string lower_name = type_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    if (lower_name == "zeromq" || lower_name == "zmq") {
        return TransportType::ZEROMQ;
    } else if (lower_name == "spmc" || lower_name == "ring") {
        return TransportType::SPMC_RING;
    } else if (lower_name == "shmem" || lower_name == "shm") {
        return TransportType::SHARED_MEMORY;
    } else {
        throw std::runtime_error("Unknown transport type: " + type_name);
    }
}

} // namespace hft