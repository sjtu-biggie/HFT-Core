#include "spmc_transport.h"
#include <iostream>
#include <cstring>
#include <thread>

namespace hft {

template<size_t RING_SIZE>
SPMCTransport<RING_SIZE>::SPMCTransport() {
    // Initialize all read positions to 0
    for (size_t i = 0; i < MAX_CONSUMERS; ++i) {
        read_positions_[i].store(0);
    }
}

template<size_t RING_SIZE>
SPMCTransport<RING_SIZE>::~SPMCTransport() {
    close();
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::initialize(const TransportConfig& config) {
    if (initialized_.load()) return true;
    
    config_ = config;
    endpoint_ = config.endpoint;
    
    // Determine role based on pattern
    if (config.pattern == TransportPattern::PUBLISH_SUBSCRIBE) {
        is_producer_ = (endpoint_.find("bind:") == 0);
        is_consumer_ = !is_producer_;
    } else {
        // Default: allow both producer and consumer capabilities
        is_producer_ = true;
        is_consumer_ = true;
    }
    
    if (is_consumer_) {
        consumer_id_ = register_consumer();
        if (consumer_id_ == UINT32_MAX) {
            std::cerr << "[SPMCTransport] Failed to register consumer" << std::endl;
            return false;
        }
    }
    
    initialized_.store(true);
    connected_.store(true);  // SPMC is always "connected"
    
    std::cout << "[SPMCTransport] Initialized with " << RING_SIZE << " byte ring buffer" << std::endl;
    return true;
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::bind(const std::string& endpoint) {
    endpoint_ = endpoint;
    is_producer_ = true;
    is_consumer_ = false;
    return true;
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::connect(const std::string& endpoint) {
    endpoint_ = endpoint;
    is_producer_ = false;
    is_consumer_ = true;
    
    if (consumer_id_ == 0) {
        consumer_id_ = register_consumer();
    }
    
    return consumer_id_ != UINT32_MAX;
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::close() {
    stop_async_receive();
    
    if (is_consumer_ && consumer_id_ != UINT32_MAX) {
        unregister_consumer(consumer_id_);
        consumer_id_ = UINT32_MAX;
    }
    
    connected_.store(false);
    initialized_.store(false);
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::send(const void* data, size_t size, bool non_blocking) {
    if (!is_producer_ || !connected_.load()) return false;
    if (size > MAX_MESSAGE_SIZE) return false;
    
    return try_push(data, size);
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::receive(void* data, size_t& size, bool non_blocking) {
    if (!is_consumer_ || !connected_.load()) return false;
    if (consumer_id_ == UINT32_MAX) return false;
    
    return try_pop(data, size, consumer_id_);
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::set_receive_callback(MessageCallback callback) {
    receive_callback_ = callback;
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::start_async_receive() {
    if (async_active_.load() || !receive_callback_ || !is_consumer_) return;
    
    async_active_.store(true);
    receive_thread_ = std::make_unique<std::thread>(&SPMCTransport::async_receive_loop, this);
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::stop_async_receive() {
    async_active_.store(false);
    if (receive_thread_ && receive_thread_->joinable()) {
        receive_thread_->join();
    }
    receive_thread_.reset();
}

template<size_t RING_SIZE>
uint32_t SPMCTransport<RING_SIZE>::register_consumer() {
    uint32_t count = consumer_count_.load();
    if (count >= MAX_CONSUMERS) {
        return UINT32_MAX;  // Too many consumers
    }
    
    // Find an available consumer slot
    for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
        uint64_t expected = 0;
        if (read_positions_[i].compare_exchange_weak(expected, write_pos_.load())) {
            consumer_count_++;
            return i;
        }
    }
    
    return UINT32_MAX;  // No available slots
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::unregister_consumer(uint32_t consumer_id) {
    if (consumer_id >= MAX_CONSUMERS) return;
    
    read_positions_[consumer_id].store(UINT64_MAX);  // Mark as inactive
    consumer_count_--;
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::empty() const {
    if (consumer_id_ == UINT32_MAX) return true;
    return read_positions_[consumer_id_].load() == write_pos_.load();
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::full() const {
    return available_space() < sizeof(MessageHeader) + 1;
}

template<size_t RING_SIZE>
size_t SPMCTransport<RING_SIZE>::available_space() const {
    uint64_t write_pos = write_pos_.load();
    uint64_t min_read_pos = get_min_read_position();
    
    if (write_pos >= min_read_pos) {
        return RING_SIZE - (write_pos - min_read_pos);
    } else {
        return min_read_pos - write_pos;
    }
}

template<size_t RING_SIZE>
size_t SPMCTransport<RING_SIZE>::used_space() const {
    return RING_SIZE - available_space();
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::async_receive_loop() {
    char buffer[MAX_MESSAGE_SIZE];
    
    while (async_active_.load() && connected_.load()) {
        size_t size = sizeof(buffer);
        
        if (receive(buffer, size, true)) {
            if (receive_callback_) {
                receive_callback_(buffer, size);
            }
        } else {
            // Small delay if no data available
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::try_push(const void* data, size_t size) {
    size_t msg_size = get_message_size_with_header(size);
    
    if (!can_write(msg_size)) {
        return false;  // Not enough space
    }
    
    uint64_t write_pos = write_pos_.load();
    size_t ring_pos = write_pos & RING_MASK;
    
    // Create message header
    MessageHeader* header = reinterpret_cast<MessageHeader*>(&ring_buffer_[ring_pos]);
    header->size = static_cast<uint32_t>(size);
    header->sequence = sequence_counter_++;
    
    // Copy data after header
    if (ring_pos + msg_size <= RING_SIZE) {
        // Message fits in one piece
        std::memcpy(header->data, data, size);
    } else {
        // Message wraps around
        size_t first_part = RING_SIZE - ring_pos - sizeof(MessageHeader);
        std::memcpy(header->data, data, first_part);
        std::memcpy(&ring_buffer_[0], static_cast<const char*>(data) + first_part, size - first_part);
    }
    
    // Advance write position atomically
    advance_write_position(msg_size);
    
    messages_sent_++;
    bytes_sent_ += size;
    
    return true;
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::try_pop(void* data, size_t& size, uint32_t consumer_id) {
    if (consumer_id >= MAX_CONSUMERS) return false;
    
    uint64_t read_pos = read_positions_[consumer_id].load();
    uint64_t write_pos = write_pos_.load();
    
    if (read_pos == write_pos) {
        return false;  // No data available
    }
    
    size_t ring_pos = read_pos & RING_MASK;
    
    // Read message header
    const MessageHeader* header = reinterpret_cast<const MessageHeader*>(&ring_buffer_[ring_pos]);
    uint32_t msg_size = header->size;
    
    if (size < msg_size) {
        return false;  // Buffer too small
    }
    
    // Copy data from ring buffer
    if (ring_pos + sizeof(MessageHeader) + msg_size <= RING_SIZE) {
        // Message fits in one piece
        std::memcpy(data, header->data, msg_size);
    } else {
        // Message wraps around
        size_t first_part = RING_SIZE - ring_pos - sizeof(MessageHeader);
        std::memcpy(data, header->data, first_part);
        std::memcpy(static_cast<char*>(data) + first_part, &ring_buffer_[0], msg_size - first_part);
    }
    
    size = msg_size;
    
    // Advance read position
    advance_read_position(consumer_id, get_message_size_with_header(msg_size));
    
    messages_received_++;
    bytes_received_ += msg_size;
    
    return true;
}

template<size_t RING_SIZE>
size_t SPMCTransport<RING_SIZE>::get_message_size_with_header(size_t data_size) const {
    return sizeof(MessageHeader) + data_size;
}

template<size_t RING_SIZE>
uint64_t SPMCTransport<RING_SIZE>::get_min_read_position() const {
    uint64_t min_pos = write_pos_.load();
    
    for (uint32_t i = 0; i < MAX_CONSUMERS; ++i) {
        uint64_t pos = read_positions_[i].load();
        if (pos != UINT64_MAX && pos < min_pos) {
            min_pos = pos;
        }
    }
    
    return min_pos;
}

template<size_t RING_SIZE>
bool SPMCTransport<RING_SIZE>::can_write(size_t required_size) const {
    return available_space() >= required_size;
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::advance_write_position(size_t size) {
    write_pos_ += size;
}

template<size_t RING_SIZE>
void SPMCTransport<RING_SIZE>::advance_read_position(uint32_t consumer_id, size_t size) {
    read_positions_[consumer_id] += size;
}

// Explicit template instantiations for common sizes
template class SPMCTransport<1024 * 1024>;
template class SPMCTransport<4 * 1024 * 1024>;
template class SPMCTransport<16 * 1024 * 1024>;

} // namespace hft