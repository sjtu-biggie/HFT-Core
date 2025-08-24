#include "logging.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace hft {

Logger::Logger(const std::string& component_name, const std::string& endpoint)
    : component_name_(component_name)
    , min_level_(LogLevel::INFO)
    , console_output_(true)
    , connected_(false) {
    
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUSH);
        
        // Set socket options for low latency
        int linger = 0;
        socket_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        socket_->connect(endpoint);
        connected_ = true;
    } catch (const zmq::error_t& e) {
        std::cerr << "Logger failed to connect to " << endpoint << ": " << e.what() << std::endl;
        connected_ = false;
    }
}

Logger::~Logger() {
    if (socket_) {
        socket_->close();
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < min_level_) {
        return;
    }
    
    // Create log message
    LogMessage log_msg = MessageFactory::create_log_message(level, component_name_, message);
    
    // Send to centralized logger if connected
    if (connected_) {
        send_log_message(log_msg);
    }
    
    // Output to console if enabled
    if (console_output_) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;
        
        std::ostringstream oss;
        oss << "[" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        oss << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
        
        // Add log level
        switch (level) {
            case LogLevel::DEBUG: oss << "[DEBUG] "; break;
            case LogLevel::INFO: oss << "[INFO]  "; break;
            case LogLevel::WARNING: oss << "[WARN]  "; break;
            case LogLevel::ERROR: oss << "[ERROR] "; break;
            case LogLevel::CRITICAL: oss << "[CRIT]  "; break;
        }
        
        oss << component_name_ << ": " << message;
        
        if (level >= LogLevel::ERROR) {
            std::cerr << oss.str() << std::endl;
        } else {
            std::cout << oss.str() << std::endl;
        }
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::critical(const std::string& message) {
    log(LogLevel::CRITICAL, message);
}

void Logger::set_log_level(LogLevel min_level) {
    min_level_ = min_level;
}

void Logger::set_console_output(bool enable) {
    console_output_ = enable;
}

void Logger::send_log_message(const LogMessage& log_msg) {
    try {
        zmq::message_t message(sizeof(LogMessage));
        std::memcpy(message.data(), &log_msg, sizeof(LogMessage));
        socket_->send(message, zmq::send_flags::dontwait);
    } catch (const zmq::error_t& e) {
        if (console_output_) {
            std::cerr << "Failed to send log message: " << e.what() << std::endl;
        }
    }
}

// Singleton implementation
GlobalLogger& GlobalLogger::instance() {
    static GlobalLogger instance;
    return instance;
}

void GlobalLogger::init(const std::string& component_name, const std::string& endpoint) {
    logger_ = std::make_unique<Logger>(component_name, endpoint);
}

Logger& GlobalLogger::get() {
    if (!logger_) {
        throw std::runtime_error("GlobalLogger not initialized. Call init() first.");
    }
    return *logger_;
}

} // namespace hft