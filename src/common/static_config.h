#pragma once

#include <string>
#include <cstring>

namespace hft {

// Static configuration class with compile-time constants for zero-cost access
// This replaces the slow hash map based Config class for performance-critical paths
class StaticConfig {
public:
    // Network endpoints - compile time constants for zero-cost access
    static constexpr const char* MARKET_DATA_ENDPOINT = "tcp://localhost:5556";
    static constexpr const char* LOGGER_ENDPOINT = "tcp://localhost:5555";
    static constexpr const char* SIGNALS_ENDPOINT = "tcp://localhost:5558";
    static constexpr const char* EXECUTIONS_ENDPOINT = "tcp://localhost:5557";
    static constexpr const char* POSITIONS_ENDPOINT = "tcp://localhost:5559";
    static constexpr const char* CONTROL_ENDPOINT = "tcp://localhost:8080";
    static constexpr const char* WEBSOCKET_ENDPOINT = "tcp://localhost:8081";
    
    // Port numbers as integers for direct use
    static constexpr int CONTROL_API_PORT = 8080;
    static constexpr int WEBSOCKET_PORT = 8081;
    static constexpr int MARKET_DATA_PORT = 5556;
    static constexpr int LOGGER_PORT = 5555;
    static constexpr int SIGNALS_PORT = 5558;
    static constexpr int EXECUTIONS_PORT = 5557;
    static constexpr int POSITIONS_PORT = 5559;
    
    // Performance tuning parameters
    static constexpr int ZMQ_SEND_HWM = 1000;
    static constexpr int ZMQ_RECV_HWM = 1000;
    static constexpr int ZMQ_LINGER_MS = 0;
    
    // Feature flags
    static constexpr bool ENABLE_DPDK = false;
    static constexpr bool ENABLE_IO_URING = false;
    static constexpr bool TRADING_ENABLED = false;
    static constexpr bool PAPER_TRADING = true;
    static constexpr bool LOG_TO_CONSOLE = true;
    
    // Transport configuration
    static constexpr const char* DEFAULT_TRANSPORT_TYPE = "zeromq";  // "zeromq" or "spmc"
    static constexpr size_t DEFAULT_RING_BUFFER_SIZE = 1024 * 1024; // 1MB for SPMC
    
    // Trading parameters
    static constexpr double MAX_POSITION_VALUE = 100000.0;
    static constexpr double MAX_DAILY_LOSS = 5000.0;
    static constexpr int POSITION_LIMIT_PER_SYMBOL = 1000;
    
    // Strategy parameters
    static constexpr double MOMENTUM_THRESHOLD = 0.001;  // 0.1%
    static constexpr int MIN_SIGNAL_INTERVAL_MS = 1000;
    
    // Mock data parameters
    static constexpr bool MOCK_DATA_ENABLED = true;
    static constexpr int MOCK_DATA_FREQUENCY_HZ = 100;
    
    // Log levels (enum converted to constexpr ints for performance)
    static constexpr int LOG_LEVEL_DEBUG = 1;
    static constexpr int LOG_LEVEL_INFO = 2;
    static constexpr int LOG_LEVEL_WARNING = 3;
    static constexpr int LOG_LEVEL_ERROR = 4;
    static constexpr int LOG_LEVEL_CRITICAL = 5;
    static constexpr int DEFAULT_LOG_LEVEL = LOG_LEVEL_INFO;
    
    // Log level string comparison (runtime function)
    static int get_log_level_from_string(const char* level) {
        if (std::strcmp(level, "DEBUG") == 0) return LOG_LEVEL_DEBUG;
        if (std::strcmp(level, "INFO") == 0) return LOG_LEVEL_INFO;
        if (std::strcmp(level, "WARNING") == 0) return LOG_LEVEL_WARNING;
        if (std::strcmp(level, "ERROR") == 0) return LOG_LEVEL_ERROR;
        if (std::strcmp(level, "CRITICAL") == 0) return LOG_LEVEL_CRITICAL;
        return DEFAULT_LOG_LEVEL;
    }
    
    // Symbol lists as compile-time arrays
    static constexpr const char* DEFAULT_SYMBOLS[] = {
        "AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "NVDA", "META", "NFLX",
        "SPY", "QQQ", "IWM", "VIX", "GLD", "TLT", "SQQQ", nullptr
    };
    
    // Get symbol count at compile time
    static constexpr size_t get_symbol_count() {
        size_t count = 0;
        while (DEFAULT_SYMBOLS[count] != nullptr) {
            count++;
        }
        return count;
    }
    
    // Runtime configuration override support (for file-based config)
    struct RuntimeOverrides {
        const char* market_data_endpoint = MARKET_DATA_ENDPOINT;
        const char* logger_endpoint = LOGGER_ENDPOINT;
        const char* signals_endpoint = SIGNALS_ENDPOINT;
        const char* executions_endpoint = EXECUTIONS_ENDPOINT;
        const char* positions_endpoint = POSITIONS_ENDPOINT;
        
        bool enable_dpdk = ENABLE_DPDK;
        bool enable_io_uring = ENABLE_IO_URING;
        bool trading_enabled = TRADING_ENABLED;
        bool paper_trading = PAPER_TRADING;
        bool mock_data_enabled = MOCK_DATA_ENABLED;
        bool log_to_console = LOG_TO_CONSOLE;
        
        int log_level = DEFAULT_LOG_LEVEL;
        int mock_data_frequency_hz = MOCK_DATA_FREQUENCY_HZ;
        
        double max_position_value = MAX_POSITION_VALUE;
        double max_daily_loss = MAX_DAILY_LOSS;
        int position_limit_per_symbol = POSITION_LIMIT_PER_SYMBOL;
        
        double momentum_threshold = MOMENTUM_THRESHOLD;
        int min_signal_interval_ms = MIN_SIGNAL_INTERVAL_MS;
        
        // Transport configuration
        const char* transport_type = DEFAULT_TRANSPORT_TYPE;
        size_t ring_buffer_size = DEFAULT_RING_BUFFER_SIZE;
    };
    
    // Global runtime overrides (for file-based configuration)
    static RuntimeOverrides runtime;
    
    // Fast access methods that check runtime overrides first, then fall back to compile-time
    static const char* get_market_data_endpoint() { return runtime.market_data_endpoint; }
    static const char* get_logger_endpoint() { return runtime.logger_endpoint; }
    static const char* get_signals_endpoint() { return runtime.signals_endpoint; }
    static const char* get_executions_endpoint() { return runtime.executions_endpoint; }
    static const char* get_positions_endpoint() { return runtime.positions_endpoint; }
    
    static bool get_enable_dpdk() { return runtime.enable_dpdk; }
    static bool get_enable_io_uring() { return runtime.enable_io_uring; }
    static bool get_trading_enabled() { return runtime.trading_enabled; }
    static bool get_paper_trading() { return runtime.paper_trading; }
    static bool get_mock_data_enabled() { return runtime.mock_data_enabled; }
    static bool get_log_to_console() { return runtime.log_to_console; }
    
    static int get_log_level() { return runtime.log_level; }
    static int get_mock_data_frequency_hz() { return runtime.mock_data_frequency_hz; }
    
    static double get_max_position_value() { return runtime.max_position_value; }
    static double get_max_daily_loss() { return runtime.max_daily_loss; }
    static int get_position_limit_per_symbol() { return runtime.position_limit_per_symbol; }
    
    static double get_momentum_threshold() { return runtime.momentum_threshold; }
    static int get_min_signal_interval_ms() { return runtime.min_signal_interval_ms; }
    
    static const char* get_transport_type() { return runtime.transport_type; }
    static size_t get_ring_buffer_size() { return runtime.ring_buffer_size; }
    
    // Load configuration from file into runtime overrides
    static bool load_from_file(const char* filename);
    
    // Validate configuration
    static bool validate_config();
    
    // Get configuration as string for debugging
    static std::string to_string();
};

// Inline definitions for better performance
inline bool StaticConfig::validate_config() {
    return runtime.market_data_endpoint != nullptr &&
           runtime.logger_endpoint != nullptr &&
           runtime.signals_endpoint != nullptr &&
           runtime.executions_endpoint != nullptr &&
           runtime.positions_endpoint != nullptr &&
           runtime.log_level >= LOG_LEVEL_DEBUG &&
           runtime.log_level <= LOG_LEVEL_CRITICAL &&
           runtime.mock_data_frequency_hz > 0 &&
           runtime.max_position_value > 0.0 &&
           runtime.max_daily_loss > 0.0 &&
           runtime.position_limit_per_symbol > 0 &&
           runtime.momentum_threshold > 0.0 &&
           runtime.min_signal_interval_ms > 0;
}

} // namespace hft