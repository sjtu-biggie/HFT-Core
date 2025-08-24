#include "low_latency_logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace hft {

LowLatencyLogger::LowLatencyLogger()
    : running_(false)
    , messages_received_(0)
    , messages_written_(0)
    , messages_dropped_(0)
#ifdef HAS_IO_URING
    , io_uring_initialized_(false)
#endif
{
}

LowLatencyLogger::~LowLatencyLogger() {
    stop();
}

bool LowLatencyLogger::initialize() {
    std::cout << "[LowLatencyLogger] Initializing Low-Latency Logger" << std::endl;
    
    config_ = std::make_unique<Config>();
    
    try {
        // Initialize ZeroMQ
        context_ = std::make_unique<zmq::context_t>(1);
        log_subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PULL);
        
        int rcvhwm = 10000;
        log_subscriber_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        
        std::string endpoint = config_->get_string(GlobalConfig::LOGGER_ENDPOINT);
        log_subscriber_->bind(endpoint);
        std::cout << "[LowLatencyLogger] Bound to " << endpoint << std::endl;
        
        // Initialize log file
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::ostringstream filename_ss;
        filename_ss << "logs/hft_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".log";
        log_filename_ = filename_ss.str();
        
        log_file_ = std::make_unique<std::ofstream>(log_filename_, std::ios::app);
        if (!log_file_->is_open()) {
            std::cerr << "[LowLatencyLogger] Failed to open log file: " << log_filename_ << std::endl;
            return false;
        }
        
        std::cout << "[LowLatencyLogger] Logging to: " << log_filename_ << std::endl;
        
        // Initialize io_uring if available and enabled
#ifdef HAS_IO_URING
        if (config_->get_bool(GlobalConfig::ENABLE_IO_URING)) {
            init_io_uring();
        }
#endif
        
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[LowLatencyLogger] ZeroMQ error: " << e.what() << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "[LowLatencyLogger] Initialization error: " << e.what() << std::endl;
        return false;
    }
}

void LowLatencyLogger::start() {
    if (running_.load()) {
        std::cout << "[LowLatencyLogger] Already running" << std::endl;
        return;
    }
    
    std::cout << "[LowLatencyLogger] Starting logger" << std::endl;
    running_.store(true);
    
    // Start receiver and writer threads
    receiver_thread_ = std::make_unique<std::thread>(&LowLatencyLogger::receive_messages, this);
    writer_thread_ = std::make_unique<std::thread>(&LowLatencyLogger::write_messages, this);
    
    std::cout << "[LowLatencyLogger] Logger started" << std::endl;
}

void LowLatencyLogger::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "[LowLatencyLogger] Stopping logger" << std::endl;
    running_.store(false);
    
    // Wake up writer thread
    queue_cv_.notify_all();
    
    if (receiver_thread_ && receiver_thread_->joinable()) {
        receiver_thread_->join();
    }
    
    if (writer_thread_ && writer_thread_->joinable()) {
        writer_thread_->join();
    }
    
    if (log_subscriber_) {
        log_subscriber_->close();
    }
    
    if (log_file_) {
        log_file_->close();
    }
    
#ifdef HAS_IO_URING
    cleanup_io_uring();
#endif
    
    log_statistics();
    std::cout << "[LowLatencyLogger] Logger stopped" << std::endl;
}

bool LowLatencyLogger::is_running() const {
    return running_.load();
}

void LowLatencyLogger::receive_messages() {
    std::cout << "[LowLatencyLogger] Message receiver thread started" << std::endl;
    
    while (running_.load()) {
        try {
            zmq::message_t message;
            if (log_subscriber_->recv(message, zmq::recv_flags::dontwait)) {
                if (message.size() == sizeof(LogMessage)) {
                    LogMessage log_msg;
                    std::memcpy(&log_msg, message.data(), sizeof(LogMessage));
                    
                    // Add to queue for async processing
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        if (message_queue_.size() < MAX_QUEUE_SIZE) {
                            message_queue_.push(log_msg);
                            messages_received_++;
                        } else {
                            messages_dropped_++;
                        }
                    }
                    queue_cv_.notify_one();
                }
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN && e.num() != EINTR) {
                std::cerr << "[LowLatencyLogger] Receive error: " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << "[LowLatencyLogger] Message receiver thread stopped" << std::endl;
}

void LowLatencyLogger::write_messages() {
    std::cout << "[LowLatencyLogger] Message writer thread started" << std::endl;
    
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(60);
    
    while (running_.load() || !message_queue_.empty()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // Wait for messages or timeout
        queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] {
            return !message_queue_.empty() || !running_.load();
        });
        
        // Process all available messages
        while (!message_queue_.empty()) {
            LogMessage msg = message_queue_.front();
            message_queue_.pop();
            lock.unlock();
            
            write_log_message(msg);
            messages_written_++;
            
            lock.lock();
        }
        
        lock.unlock();
        
        // Log statistics periodically
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            log_statistics();
            last_stats_time = now;
        }
    }
    
    std::cout << "[LowLatencyLogger] Message writer thread stopped" << std::endl;
}

void LowLatencyLogger::write_log_message(const LogMessage& msg) {
    if (!log_file_ || !log_file_->is_open()) {
        return;
    }
    
    std::string formatted = format_log_message(msg);
    
#ifdef HAS_IO_URING
    if (io_uring_initialized_) {
        // TODO: Implement async write with io_uring
        // For now, fall back to synchronous write
        *log_file_ << formatted << std::endl;
        log_file_->flush();
    } else {
#endif
        *log_file_ << formatted << std::endl;
        log_file_->flush();
#ifdef HAS_IO_URING
    }
#endif
}

std::string LowLatencyLogger::format_log_message(const LogMessage& msg) {
    auto time_point = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        time_point.time_since_epoch()
    ) % 1000;
    
    std::ostringstream oss;
    oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    
    // Add level
    switch (msg.level) {
        case LogLevel::DEBUG: oss << "[DEBUG] "; break;
        case LogLevel::INFO: oss << "[INFO]  "; break;
        case LogLevel::WARNING: oss << "[WARN]  "; break;
        case LogLevel::ERROR: oss << "[ERROR] "; break;
        case LogLevel::CRITICAL: oss << "[CRIT]  "; break;
    }
    
    oss << msg.component << ": " << msg.message;
    return oss.str();
}

void LowLatencyLogger::init_io_uring() {
#ifdef HAS_IO_URING
    if (io_uring_queue_init(256, &ring_, 0) < 0) {
        std::cerr << "[LowLatencyLogger] Failed to initialize io_uring" << std::endl;
        io_uring_initialized_ = false;
    } else {
        io_uring_initialized_ = true;
        std::cout << "[LowLatencyLogger] io_uring initialized successfully" << std::endl;
    }
#endif
}

void LowLatencyLogger::cleanup_io_uring() {
#ifdef HAS_IO_URING
    if (io_uring_initialized_) {
        io_uring_queue_exit(&ring_);
        io_uring_initialized_ = false;
    }
#endif
}

void LowLatencyLogger::log_statistics() {
    uint64_t received = messages_received_.load();
    uint64_t written = messages_written_.load();
    uint64_t dropped = messages_dropped_.load();
    
    std::string stats = "[LowLatencyLogger] Stats: received=" + std::to_string(received) +
                       ", written=" + std::to_string(written) +
                       ", dropped=" + std::to_string(dropped) +
                       ", queue_size=" + std::to_string(message_queue_.size());
    
    std::cout << stats << std::endl;
    
    if (log_file_ && log_file_->is_open()) {
        *log_file_ << stats << std::endl;
    }
}

} // namespace hft