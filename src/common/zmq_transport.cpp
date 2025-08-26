#include "zmq_transport.h"
#include <iostream>
#include <chrono>

namespace hft {

ZmqTransportBase::ZmqTransportBase(int socket_type) 
    : socket_type_(socket_type) {
}

ZmqTransportBase::~ZmqTransportBase() {
    close();
}

bool ZmqTransportBase::initialize(const TransportConfig& config) {
    if (initialized_.load()) return true;
    
    config_ = config;
    endpoint_ = config.endpoint;
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, socket_type_);
        
        if (!configure_socket()) {
            return false;
        }
        
        initialized_.store(true);
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqTransport] Initialization error: " << e.what() << std::endl;
        return false;
    }
}

bool ZmqTransportBase::bind(const std::string& endpoint) {
    if (!initialized_.load()) return false;
    
    try {
        socket_->bind(endpoint);
        endpoint_ = endpoint;
        connected_.store(true);
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqTransport] Bind error: " << e.what() << std::endl;
        return false;
    }
}

bool ZmqTransportBase::connect(const std::string& endpoint) {
    if (!initialized_.load()) return false;
    
    try {
        socket_->connect(endpoint);
        endpoint_ = endpoint;
        connected_.store(true);
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqTransport] Connect error: " << e.what() << std::endl;
        return false;
    }
}

void ZmqTransportBase::close() {
    stop_async_receive();
    
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    
    context_.reset();
    connected_.store(false);
    initialized_.store(false);
}

bool ZmqTransportBase::send(const void* data, size_t size, bool non_blocking) {
    if (!connected_.load()) return false;
    
    try {
        zmq::message_t message(size);
        std::memcpy(message.data(), data, size);
        
        auto flags = non_blocking ? zmq::send_flags::dontwait : zmq::send_flags::none;
        bool result = socket_->send(message, flags).has_value();
        
        if (result) {
            messages_sent_++;
            bytes_sent_ += size;
        }
        
        return result;
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN || !non_blocking) {
            std::cerr << "[ZmqTransport] Send error: " << e.what() << std::endl;
        }
        return false;
    }
}

bool ZmqTransportBase::receive(void* data, size_t& size, bool non_blocking) {
    if (!connected_.load()) return false;
    
    try {
        zmq::message_t message;
        auto flags = non_blocking ? zmq::recv_flags::dontwait : zmq::recv_flags::none;
        
        if (socket_->recv(message, flags)) {
            if (message.size() <= size) {
                std::memcpy(data, message.data(), message.size());
                size = message.size();
                
                messages_received_++;
                bytes_received_ += size;
                return true;
            } else {
                std::cerr << "[ZmqTransport] Receive buffer too small" << std::endl;
                return false;
            }
        }
        return false;
        
    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN || !non_blocking) {
            std::cerr << "[ZmqTransport] Receive error: " << e.what() << std::endl;
        }
        return false;
    }
}

void ZmqTransportBase::set_receive_callback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    receive_callback_ = callback;
}

void ZmqTransportBase::start_async_receive() {
    if (async_active_.load() || !receive_callback_) return;
    
    async_active_.store(true);
    receive_thread_ = std::make_unique<std::thread>(&ZmqTransportBase::async_receive_loop, this);
}

void ZmqTransportBase::stop_async_receive() {
    async_active_.store(false);
    if (receive_thread_ && receive_thread_->joinable()) {
        receive_thread_->join();
    }
    receive_thread_.reset();
}

void* ZmqTransportBase::get_native_handle() {
    return socket_ ? static_cast<void*>(*socket_) : nullptr;
}

void ZmqTransportBase::async_receive_loop() {
    char buffer[64 * 1024];  // 64KB buffer
    
    while (async_active_.load() && connected_.load()) {
        size_t size = sizeof(buffer);
        
        if (receive(buffer, size, true)) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (receive_callback_) {
                receive_callback_(buffer, size);
            }
        } else {
            // Small delay if no data available
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

bool ZmqTransportBase::configure_socket() {
    try {
        // Set high water marks
        int hwm = config_.high_water_mark;
        socket_->set(zmq::sockopt::sndhwm, hwm);
        socket_->set(zmq::sockopt::rcvhwm, hwm);
        
        // Set linger time (don't wait on close)
        socket_->set(zmq::sockopt::linger, 0);
        
        // Configure based on socket type
        if (socket_type_ == ZMQ_SUB) {
            // Subscribe to all messages by default
            socket_->set(zmq::sockopt::subscribe, "");
        }
        
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqTransport] Socket configuration error: " << e.what() << std::endl;
        return false;
    }
}

// ZmqPublisher implementation
bool ZmqPublisher::publish(const void* data, size_t size) {
    return send(data, size, true);  // Non-blocking by default
}

bool ZmqPublisher::publish(const std::string& topic, const void* data, size_t size) {
    try {
        // Send topic first, then data
        zmq::message_t topic_msg(topic.size());
        std::memcpy(topic_msg.data(), topic.c_str(), topic.size());
        
        zmq::message_t data_msg(size);
        std::memcpy(data_msg.data(), data, size);
        
        bool result = socket_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait).has_value();
        if (result) {
            result = socket_->send(data_msg, zmq::send_flags::dontwait).has_value();
        }
        
        if (result) {
            messages_sent_++;
            bytes_sent_ += topic.size() + size;
        }
        
        return result;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqPublisher] Publish error: " << e.what() << std::endl;
        return false;
    }
}

void ZmqPublisher::set_filter(const std::string& filter) {
    // Publishers don't use filters in ZMQ
}

// ZmqSubscriber implementation
bool ZmqSubscriber::subscribe(const std::string& topic) {
    if (!socket_) return false;
    
    try {
        socket_->set(zmq::sockopt::subscribe, topic);
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqSubscriber] Subscribe error: " << e.what() << std::endl;
        return false;
    }
}

bool ZmqSubscriber::unsubscribe(const std::string& topic) {
    if (!socket_) return false;
    
    try {
        socket_->set(zmq::sockopt::unsubscribe, topic);
        return true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZmqSubscriber] Unsubscribe error: " << e.what() << std::endl;
        return false;
    }
}

// ZmqPusher implementation
bool ZmqPusher::push(const void* data, size_t size) {
    return send(data, size, true);  // Non-blocking by default
}

// ZmqPuller implementation
bool ZmqPuller::pull(void* data, size_t& size, bool non_blocking) {
    return receive(data, size, non_blocking);
}

} // namespace hft