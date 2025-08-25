#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include <array>

namespace hft {

// High-resolution timestamp type
using timestamp_t = std::chrono::nanoseconds;

// Message type enumeration for fast dispatch
enum class MessageType : uint16_t {
    MARKET_DATA = 1,
    TRADING_SIGNAL = 2,
    ORDER_REQUEST = 3,
    ORDER_EXECUTION = 4,
    POSITION_UPDATE = 5,
    RISK_ALERT = 6,
    LOG_MESSAGE = 7,
    CONTROL_COMMAND = 8,
    SYSTEM_STATUS = 9
};

// Common message header for all internal messages
struct MessageHeader {
    MessageType type;
    uint32_t sequence_number;
    timestamp_t timestamp;
    uint16_t payload_size;
} __attribute__((packed));

// Market data message - normalized format from various sources
struct MarketData {
    MessageHeader header;
    char symbol[16];           // Symbol name (null-terminated)
    double bid_price;          // Best bid price
    double ask_price;          // Best ask price
    uint32_t bid_size;         // Best bid size
    uint32_t ask_size;         // Best ask size
    double last_price;         // Last trade price
    uint32_t last_size;        // Last trade size
    uint64_t exchange_timestamp; // Exchange timestamp in nanoseconds
} __attribute__((packed));

// Trading signal - output from strategy engine
enum class SignalAction : uint8_t {
    BUY = 1,
    SELL = 2,
    CANCEL = 3,
    MODIFY = 4
};

enum class OrderType : uint8_t {
    MARKET = 1,
    LIMIT = 2,
    STOP = 3,
    STOP_LIMIT = 4
};

struct TradingSignal {
    MessageHeader header;
    char symbol[16];           // Symbol to trade
    SignalAction action;       // What action to take
    OrderType order_type;      // Type of order
    double price;              // Limit price (0.0 for market orders)
    uint32_t quantity;         // Number of shares
    uint64_t strategy_id;      // ID of generating strategy
    double confidence;         // Signal confidence [0.0, 1.0]
} __attribute__((packed));

// Order execution report from broker
enum class ExecutionType : uint8_t {
    NEW = 1,
    PARTIAL_FILL = 2,
    FILL = 3,
    CANCELLED = 4,
    REJECTED = 5
};

struct OrderExecution {
    MessageHeader header;
    uint64_t order_id;         // Unique order identifier
    char symbol[16];           // Symbol traded
    ExecutionType exec_type;   // Type of execution
    double fill_price;         // Price of execution
    uint32_t fill_quantity;    // Quantity executed
    uint32_t remaining_quantity; // Quantity remaining
    double commission;         // Commission charged
} __attribute__((packed));

// Position update from position service
struct PositionUpdate {
    MessageHeader header;
    char symbol[16];           // Symbol
    int32_t position;          // Net position (positive = long, negative = short)
    double average_price;      // Average entry price
    double unrealized_pnl;     // Unrealized P&L
    double realized_pnl;       // Realized P&L (session)
    double market_value;       // Current market value
} __attribute__((packed));

// Risk alert from risk service
enum class RiskLevel : uint8_t {
    INFO = 1,
    WARNING = 2,
    CRITICAL = 3
};

struct RiskAlert {
    MessageHeader header;
    RiskLevel level;           // Severity of alert
    char message[128];         // Alert description
    char symbol[16];           // Related symbol (if applicable)
    double threshold_value;    // Threshold that was breached
    double current_value;      // Current value
} __attribute__((packed));

// Log message for centralized logging
enum class LogLevel : uint8_t {
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

struct LogMessage {
    MessageHeader header;
    LogLevel level;            // Log level
    char component[32];        // Component name that generated log
    char message[256];         // Log message content
} __attribute__((packed));

// Control command from dashboard/API
enum class ControlAction : uint8_t {
    START_TRADING = 1,
    STOP_TRADING = 2,
    PAUSE_TRADING = 3,
    RESTART_SERVICE = 4,
    SHUTDOWN_SYSTEM = 5,
    UPDATE_CONFIG = 6
};

struct ControlCommand {
    MessageHeader header;
    ControlAction action;      // Command to execute
    char target_service[32];   // Service to target (or "all")
    char parameters[128];      // Additional parameters (JSON format)
} __attribute__((packed));

// System status for monitoring
enum class ServiceStatus : uint8_t {
    STARTING = 1,
    RUNNING = 2,
    PAUSED = 3,
    ERROR = 4,
    SHUTDOWN = 5
};

struct SystemStatus {
    MessageHeader header;
    char service_name[32];     // Name of reporting service
    ServiceStatus status;      // Current status
    uint64_t messages_processed; // Messages processed since start
    uint64_t memory_usage_kb;  // Memory usage in KB
    double cpu_usage_percent;  // CPU usage percentage
    timestamp_t uptime;        // Service uptime
} __attribute__((packed));

// Union for type-safe message handling
union Message {
    MessageHeader header;
    MarketData market_data;
    TradingSignal trading_signal;
    OrderExecution order_execution;
    PositionUpdate position_update;
    RiskAlert risk_alert;
    LogMessage log_message;
    ControlCommand control_command;
    SystemStatus system_status;
};

// Helper functions for message creation and validation
class MessageFactory {
public:
    static MessageHeader create_header(MessageType type, uint16_t payload_size);
    static MarketData create_market_data(const std::string& symbol,
                                       double bid, double ask,
                                       uint32_t bid_size, uint32_t ask_size,
                                       double last_price, uint32_t last_size);
    static TradingSignal create_trading_signal(const std::string& symbol,
                                             SignalAction action,
                                             OrderType type,
                                             double price,
                                             uint32_t quantity,
                                             uint64_t strategy_id,
                                             double confidence = 1.0);
    static LogMessage create_log_message(LogLevel level,
                                       const std::string& component,
                                       const std::string& message);
    
    static bool validate_message(const Message& msg);
    static std::string message_to_string(const Message& msg);
};

} // namespace hft