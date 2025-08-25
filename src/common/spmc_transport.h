#pragma once

#include "transport_interface.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>

namespace hft {

// SPMC (Single Producer, Multiple Consumer) transport implementation
template<size_t RING_SIZE = 1024 * 1024>  // 1MB default ring buffer
class SPMCTransport : public IMessageTransport {
    static_assert((RING_SIZE & (RING_SIZE - 1)) == 0, "RING_SIZE must be power of 2");
    
public:
    SPMCTransport();
    virtual ~SPMCTransport();
    
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
    TransportType get_type() const override { return TransportType::SPMC_RING; }
    std::string get_endpoint() const override { return endpoint_; }
    
    uint64_t get_messages_sent() const override { return messages_sent_.load(); }
    uint64_t get_messages_received() const override { return messages_received_.load(); }
    uint64_t get_bytes_sent() const override { return bytes_sent_.load(); }
    uint64_t get_bytes_received() const override { return bytes_received_.load(); }
    
    void* get_native_handle() override { return nullptr; }  // No native handle for SPMC

    // SPMC-specific methods
    uint32_t register_consumer();
    void unregister_consumer(uint32_t consumer_id);
    
    // Ring buffer status
    bool empty() const;
    bool full() const;
    size_t available_space() const;
    size_t used_space() const;

private:
    // Message header in ring buffer
    struct MessageHeader {
        uint32_t size;
        uint32_t sequence;
        char data[];  // Flexible array member (C99)
    } __attribute__((packed));
    
    // Ring buffer structure
    alignas(64) char ring_buffer_[RING_SIZE];
    alignas(64) std::atomic<uint64_t> write_pos_{0};
    alignas(64) std::atomic<uint64_t> read_positions_[32];  // Up to 32 consumers
    alignas(64) std::atomic<uint32_t> consumer_count_{0};
    alignas(64) std::atomic<uint32_t> next_consumer_id_{0};
    alignas(64) std::atomic<uint32_t> sequence_counter_{0};
    
    // Configuration and state
    TransportConfig config_;
    std::string endpoint_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> initialized_{false};
    
    // Statistics
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_received_{0};
    std::atomic<uint64_t> bytes_sent_{0};
    std::atomic<uint64_t> bytes_received_{0};
    
    // Consumer management
    uint32_t consumer_id_{0};
    bool is_consumer_{false};
    bool is_producer_{false};
    
    // Async receive
    MessageCallback receive_callback_;
    std::unique_ptr<std::thread> receive_thread_;
    std::atomic<bool> async_active_{false};
    
    // Constants
    static constexpr size_t RING_MASK = RING_SIZE - 1;
    static constexpr size_t MAX_MESSAGE_SIZE = RING_SIZE / 4;  // Max 25% of ring per message
    static constexpr uint32_t MAX_CONSUMERS = 32;
    
    // Helper methods
    void async_receive_loop();
    bool try_push(const void* data, size_t size);
    bool try_pop(void* data, size_t& size, uint32_t consumer_id);
    size_t get_message_size_with_header(size_t data_size) const;
    uint64_t get_min_read_position() const;
    bool can_write(size_t required_size) const;
    void advance_write_position(size_t size);
    void advance_read_position(uint32_t consumer_id, size_t size);
};

// SPMC Publisher (single producer)
template<size_t RING_SIZE = 1024 * 1024>
class SPMCPublisher : public virtual SPMCTransport<RING_SIZE>, public virtual IMessagePublisher {
public:
    bool publish(const void* data, size_t size) override {
        return SPMCTransport<RING_SIZE>::send(data, size, true);  // Non-blocking by default
    }
    
    bool publish(const std::string& topic, const void* data, size_t size) override {
        // For SPMC, topics are handled at application level
        // Just append topic to the beginning of data
        std::vector<char> msg_buffer(topic.size() + 1 + size);
        std::memcpy(msg_buffer.data(), topic.c_str(), topic.size());
        msg_buffer[topic.size()] = '\0';  // Null terminate topic
        std::memcpy(msg_buffer.data() + topic.size() + 1, data, size);
        
        return publish(msg_buffer.data(), msg_buffer.size());
    }
    
    void set_filter(const std::string& filter) override {
        // SPMC doesn't support server-side filtering like ZMQ
        // This would need to be implemented at application level
    }
};

// SPMC Subscriber (multiple consumers)
template<size_t RING_SIZE = 1024 * 1024>
class SPMCSubscriber : public virtual SPMCTransport<RING_SIZE>, public virtual IMessageSubscriber {
public:
    bool subscribe(const std::string& topic = "") override {
        topic_filter_ = topic;
        return this->register_consumer() != UINT32_MAX;
    }
    
    bool unsubscribe(const std::string& topic = "") override {
        // For SPMC, unsubscribing means unregistering as consumer
        if (consumer_id_ != UINT32_MAX) {
            this->unregister_consumer(consumer_id_);
            consumer_id_ = UINT32_MAX;
        }
        return true;
    }
    
    bool receive(void* data, size_t& size, bool non_blocking = false) override {
        // Override to add topic filtering
        char temp_buffer[RING_SIZE / 4];  // Temporary buffer for filtering
        size_t temp_size = sizeof(temp_buffer);
        
        if (SPMCTransport<RING_SIZE>::receive(temp_buffer, temp_size, non_blocking)) {
            // Check topic filter if set
            if (!topic_filter_.empty()) {
                if (temp_size < topic_filter_.size() + 1) return false;
                if (std::strncmp(temp_buffer, topic_filter_.c_str(), topic_filter_.size()) != 0) {
                    return false;  // Topic doesn't match
                }
                
                // Skip topic and null terminator
                size_t data_offset = topic_filter_.size() + 1;
                size_t actual_size = temp_size - data_offset;
                
                if (size < actual_size) return false;  // Buffer too small
                
                std::memcpy(data, temp_buffer + data_offset, actual_size);
                size = actual_size;
            } else {
                // No filtering, copy all data
                if (size < temp_size) return false;
                std::memcpy(data, temp_buffer, temp_size);
                size = temp_size;
            }
            return true;
        }
        return false;
    }

private:
    std::string topic_filter_;
    uint32_t consumer_id_ = UINT32_MAX;
};

// Type aliases for common ring buffer sizes
using SPMC1M = SPMCTransport<1024 * 1024>;      // 1MB ring
using SPMC4M = SPMCTransport<4 * 1024 * 1024>;  // 4MB ring
using SPMC16M = SPMCTransport<16 * 1024 * 1024>; // 16MB ring

using SPMCPublisher1M = SPMCPublisher<1024 * 1024>;
using SPMCSubscriber1M = SPMCSubscriber<1024 * 1024>;

} // namespace hft