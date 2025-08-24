#pragma once

#include "../common/message_types.h"
#include "../common/config.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <fstream>

#ifdef HAS_IO_URING
#include <liburing.h>
#endif

namespace hft {

class LowLatencyLogger {
public:
    LowLatencyLogger();
    ~LowLatencyLogger();
    
    bool initialize();
    void start();
    void stop();
    bool is_running() const;

private:
    std::unique_ptr<Config> config_;
    
    // ZeroMQ socket for receiving log messages
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> log_subscriber_;
    
    // Threading
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> receiver_thread_;
    std::unique_ptr<std::thread> writer_thread_;
    
    // Message queue for async processing
    std::queue<LogMessage> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    static constexpr size_t MAX_QUEUE_SIZE = 10000;
    
    // File handling
    std::unique_ptr<std::ofstream> log_file_;
    std::string log_filename_;
    
    // Statistics
    std::atomic<uint64_t> messages_received_;
    std::atomic<uint64_t> messages_written_;
    std::atomic<uint64_t> messages_dropped_;
    
    // io_uring specific (if available)
#ifdef HAS_IO_URING
    struct io_uring ring_;
    bool io_uring_initialized_;
#endif
    
    void receive_messages();
    void write_messages();
    void write_log_message(const LogMessage& msg);
    void init_io_uring();
    void cleanup_io_uring();
    void log_statistics();
    
    std::string format_log_message(const LogMessage& msg);
};

} // namespace hft