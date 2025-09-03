#pragma once

#include "message_types.h"
#include "static_config.h"

#include <memory>
#include <string>
#include <zmq.hpp>

namespace hft {

class Logger {
public:
    Logger(const std::string& component_name, const std::string& endpoint);
    ~Logger();
    
    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
    // Set minimum log level to filter messages
    void set_log_level(LogLevel min_level);
    
    // Enable/disable console output
    void set_console_output(bool enable);

private:
    std::string component_name_;
    LogLevel min_level_;
    bool console_output_;
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    bool connected_;
    
    void send_log_message(const LogMessage& log_msg);
};

// Singleton logger for easy access across the application
class GlobalLogger {
public:
    static GlobalLogger& instance();
    void init(const std::string& component_name, const std::string& endpoint = "tcp://localhost:5555");
    Logger& get();

private:
    GlobalLogger() = default;
    std::unique_ptr<Logger> logger_;
};

// Convenience macros for logging
#define HFT_LOG_DEBUG(msg) GlobalLogger::instance().get().debug(msg)
#define HFT_LOG_INFO(msg) GlobalLogger::instance().get().info(msg)
#define HFT_LOG_WARNING(msg) GlobalLogger::instance().get().warning(msg)
#define HFT_LOG_ERROR(msg) GlobalLogger::instance().get().error(msg)
#define HFT_LOG_CRITICAL(msg) GlobalLogger::instance().get().critical(msg)

} // namespace hft