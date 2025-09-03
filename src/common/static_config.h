#pragma once

#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>

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
    
    // Metrics publisher ports (one per service)
    static constexpr int STRATEGY_ENGINE_METRICS_PORT = 5561;
    static constexpr int MARKET_DATA_HANDLER_METRICS_PORT = 5562;
    static constexpr int ORDER_GATEWAY_METRICS_PORT = 5563;
    static constexpr int POSITION_RISK_SERVICE_METRICS_PORT = 5564;
    static constexpr int METRICS_AGGREGATOR_PORT = 5560;
    static constexpr int CONTROL_COMMANDS_PORT = 5570;
    
    // Service-specific timing parameters (milliseconds)
    static constexpr int DEFAULT_POLL_TIMEOUT_MS = 100;
    static constexpr int STATS_INTERVAL_SECONDS = 10;
    static constexpr int CONTROL_POLL_INTERVAL_MS = 10;
    static constexpr int PROCESSING_SLEEP_MICROSECONDS = 100;
    static constexpr int FAST_PROCESSING_SLEEP_MICROSECONDS = 50;
    static constexpr int ORDER_EXECUTION_MIN_DELAY_MS = 10;
    static constexpr int ORDER_EXECUTION_MAX_DELAY_MS = 100;
    static constexpr int METRICS_UPDATE_INTERVAL_SECONDS = 5;
    static constexpr int METRICS_PUBLISHER_INTERVAL_MS = 2000;
    
    // Mock data generation parameters
    static constexpr double DEFAULT_PRICE_CHANGE_VOLATILITY = 0.01; // 1%
    static constexpr double MIN_PRICE_MULTIPLIER = 0.5; // 50% of base price
    static constexpr double MAX_PRICE_MULTIPLIER = 2.0; // 200% of base price
    static constexpr double BASE_SPREAD_BASIS_POINTS = 5.0;
    static constexpr int MIN_VOLUME = 1000;
    static constexpr int MAX_VOLUME = 5000;
    static constexpr int MIN_LAST_SIZE = 100;
    static constexpr int MAX_LAST_SIZE = 1000;
    
    // Alpaca API configuration
    static constexpr int ALPACA_MAX_SYMBOLS_PER_REQUEST = 30;
    static constexpr int ALPACA_MAX_MESSAGE_SIZE_KB = 15; // 15KB (below 16KB limit)
    static constexpr int ALPACA_RECONNECT_INTERVAL_SECONDS = 30;
    static constexpr int ALPACA_MAX_RECONNECT_ATTEMPTS = 10;
    static constexpr int ALPACA_AUTH_TIMEOUT_SECONDS = 10;
    static constexpr int ALPACA_RATE_LIMIT_PER_MINUTE = 200;
    static constexpr int ALPACA_CIRCUIT_BREAKER_FAILURES = 5;
    static constexpr int ALPACA_CIRCUIT_BREAKER_TIMEOUT_MINUTES = 1;
    
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
        
        // String storage for dynamically loaded endpoint configurations
        std::string market_data_endpoint_storage;
        std::string logger_endpoint_storage;
        std::string signals_endpoint_storage;
        std::string executions_endpoint_storage;
        std::string positions_endpoint_storage;
        
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
        
        // Market data source configuration
        std::string market_data_source = "mock";  // "mock", "pcap", "alpaca"
        std::string pcap_file_path = "data/market_data.pcap";
        std::string pcap_format = "generic_csv";  // "generic_csv", "nasdaq_itch", "nyse_pillar", "iex_tops", "fix"
        double replay_speed = 1.0;
        bool loop_replay = false;
        
        // Metrics publisher ports
        int strategy_engine_metrics_port = STRATEGY_ENGINE_METRICS_PORT;
        int market_data_handler_metrics_port = MARKET_DATA_HANDLER_METRICS_PORT;
        int order_gateway_metrics_port = ORDER_GATEWAY_METRICS_PORT;
        int position_risk_service_metrics_port = POSITION_RISK_SERVICE_METRICS_PORT;
        int metrics_aggregator_port = METRICS_AGGREGATOR_PORT;
        int control_commands_port = CONTROL_COMMANDS_PORT;
        
        // Timing parameters
        int poll_timeout_ms = DEFAULT_POLL_TIMEOUT_MS;
        int stats_interval_seconds = STATS_INTERVAL_SECONDS;
        int control_poll_interval_ms = CONTROL_POLL_INTERVAL_MS;
        int processing_sleep_microseconds = PROCESSING_SLEEP_MICROSECONDS;
        int fast_processing_sleep_microseconds = FAST_PROCESSING_SLEEP_MICROSECONDS;
        int order_execution_min_delay_ms = ORDER_EXECUTION_MIN_DELAY_MS;
        int order_execution_max_delay_ms = ORDER_EXECUTION_MAX_DELAY_MS;
        int metrics_update_interval_seconds = METRICS_UPDATE_INTERVAL_SECONDS;
        int metrics_publisher_interval_ms = METRICS_PUBLISHER_INTERVAL_MS;
        
        // Mock data generation parameters
        double price_change_volatility = DEFAULT_PRICE_CHANGE_VOLATILITY;
        double min_price_multiplier = MIN_PRICE_MULTIPLIER;
        double max_price_multiplier = MAX_PRICE_MULTIPLIER;
        double base_spread_basis_points = BASE_SPREAD_BASIS_POINTS;
        int min_volume = MIN_VOLUME;
        int max_volume = MAX_VOLUME;
        int min_last_size = MIN_LAST_SIZE;
        int max_last_size = MAX_LAST_SIZE;
        
        // Symbol list and prices (configurable)
        std::vector<std::string> symbols{DEFAULT_SYMBOLS, DEFAULT_SYMBOLS + get_symbol_count()};
        std::unordered_map<std::string, double> symbol_base_prices = {
            {"AAPL", 175.0}, {"GOOGL", 140.0}, {"MSFT", 380.0}, {"TSLA", 250.0},
            {"AMZN", 145.0}, {"NVDA", 900.0}, {"META", 350.0}, {"NFLX", 450.0},
            {"SPY", 450.0}, {"QQQ", 380.0}, {"IWM", 200.0}, {"GLD", 180.0},
            {"TLT", 95.0}, {"VIX", 18.0}, {"TQQQ", 45.0}, {"SQQQ", 12.0}
        };
        std::unordered_map<std::string, double> symbol_volatilities = {
            {"AAPL", 0.25}, {"GOOGL", 0.28}, {"MSFT", 0.22}, {"TSLA", 0.45},
            {"AMZN", 0.30}, {"NVDA", 0.35}, {"META", 0.32}, {"NFLX", 0.68},
            {"SPY", 0.15}, {"QQQ", 0.20}, {"IWM", 0.25}, {"GLD", 0.18},
            {"TLT", 0.12}, {"VIX", 0.80}, {"TQQQ", 0.60}, {"SQQQ", 0.60}
        };
        
        // Alpaca API configuration
        std::string alpaca_api_key;
        std::string alpaca_secret_key;
        bool alpaca_paper_trading = true;
        std::string alpaca_websocket_feed = "iex"; // "iex" (free) or "sip" (paid)
        std::string alpaca_websocket_url = "wss://stream.data.alpaca.markets/v2/iex";
        std::string alpaca_websocket_host = "stream.data.alpaca.markets";
        int alpaca_max_symbols_per_request = ALPACA_MAX_SYMBOLS_PER_REQUEST;
        int alpaca_max_message_size_kb = ALPACA_MAX_MESSAGE_SIZE_KB;
        int alpaca_reconnect_interval_seconds = ALPACA_RECONNECT_INTERVAL_SECONDS;
        int alpaca_max_reconnect_attempts = ALPACA_MAX_RECONNECT_ATTEMPTS;
        int alpaca_auth_timeout_seconds = ALPACA_AUTH_TIMEOUT_SECONDS;
        int alpaca_rate_limit_per_minute = ALPACA_RATE_LIMIT_PER_MINUTE;
        int alpaca_circuit_breaker_failures = ALPACA_CIRCUIT_BREAKER_FAILURES;
        int alpaca_circuit_breaker_timeout_minutes = ALPACA_CIRCUIT_BREAKER_TIMEOUT_MINUTES;
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
    
    // Market data source configuration getters
    static const std::string& get_market_data_source() { return runtime.market_data_source; }
    static const std::string& get_pcap_file_path() { return runtime.pcap_file_path; }
    static const std::string& get_pcap_format() { return runtime.pcap_format; }
    static double get_replay_speed() { return runtime.replay_speed; }
    static bool get_loop_replay() { return runtime.loop_replay; }
    
    // Metrics publisher port getters
    static int get_strategy_engine_metrics_port() { return runtime.strategy_engine_metrics_port; }
    static int get_market_data_handler_metrics_port() { return runtime.market_data_handler_metrics_port; }
    static int get_order_gateway_metrics_port() { return runtime.order_gateway_metrics_port; }
    static int get_position_risk_service_metrics_port() { return runtime.position_risk_service_metrics_port; }
    static int get_metrics_aggregator_port() { return runtime.metrics_aggregator_port; }
    static int get_control_commands_port() { return runtime.control_commands_port; }
    
    // Timing parameter getters
    static int get_poll_timeout_ms() { return runtime.poll_timeout_ms; }
    static int get_stats_interval_seconds() { return runtime.stats_interval_seconds; }
    static int get_control_poll_interval_ms() { return runtime.control_poll_interval_ms; }
    static int get_processing_sleep_microseconds() { return runtime.processing_sleep_microseconds; }
    static int get_fast_processing_sleep_microseconds() { return runtime.fast_processing_sleep_microseconds; }
    static int get_order_execution_min_delay_ms() { return runtime.order_execution_min_delay_ms; }
    static int get_order_execution_max_delay_ms() { return runtime.order_execution_max_delay_ms; }
    static int get_metrics_update_interval_seconds() { return runtime.metrics_update_interval_seconds; }
    static int get_metrics_publisher_interval_ms() { return runtime.metrics_publisher_interval_ms; }
    
    // Mock data generation parameter getters
    static double get_price_change_volatility() { return runtime.price_change_volatility; }
    static double get_min_price_multiplier() { return runtime.min_price_multiplier; }
    static double get_max_price_multiplier() { return runtime.max_price_multiplier; }
    static double get_base_spread_basis_points() { return runtime.base_spread_basis_points; }
    static int get_min_volume() { return runtime.min_volume; }
    static int get_max_volume() { return runtime.max_volume; }
    static int get_min_last_size() { return runtime.min_last_size; }
    static int get_max_last_size() { return runtime.max_last_size; }
    
    // Symbol configuration getters
    static const std::vector<std::string>& get_symbols() { return runtime.symbols; }
    static const std::unordered_map<std::string, double>& get_symbol_base_prices() { return runtime.symbol_base_prices; }
    static const std::unordered_map<std::string, double>& get_symbol_volatilities() { return runtime.symbol_volatilities; }
    
    // Alpaca API configuration getters
    static const std::string& get_alpaca_api_key() { return runtime.alpaca_api_key; }
    static const std::string& get_alpaca_secret_key() { return runtime.alpaca_secret_key; }
    static bool get_alpaca_paper_trading() { return runtime.alpaca_paper_trading; }
    static const std::string& get_alpaca_websocket_feed() { return runtime.alpaca_websocket_feed; }
    static const std::string& get_alpaca_websocket_url() { return runtime.alpaca_websocket_url; }
    static const std::string& get_alpaca_websocket_host() { return runtime.alpaca_websocket_host; }
    static int get_alpaca_max_symbols_per_request() { return runtime.alpaca_max_symbols_per_request; }
    static int get_alpaca_max_message_size_kb() { return runtime.alpaca_max_message_size_kb; }
    static int get_alpaca_reconnect_interval_seconds() { return runtime.alpaca_reconnect_interval_seconds; }
    static int get_alpaca_max_reconnect_attempts() { return runtime.alpaca_max_reconnect_attempts; }
    static int get_alpaca_auth_timeout_seconds() { return runtime.alpaca_auth_timeout_seconds; }
    static int get_alpaca_rate_limit_per_minute() { return runtime.alpaca_rate_limit_per_minute; }
    static int get_alpaca_circuit_breaker_failures() { return runtime.alpaca_circuit_breaker_failures; }
    static int get_alpaca_circuit_breaker_timeout_minutes() { return runtime.alpaca_circuit_breaker_timeout_minutes; }
    
    // Generic configuration value getters (with defaults)
    static std::string get_config_value(const std::string& key, const std::string& default_value) {
        if (key == "market_data.source") return runtime.market_data_source;
        if (key == "market_data.pcap_file") return runtime.pcap_file_path;
        if (key == "market_data.pcap_format") return runtime.pcap_format;
        if (key == "market_data.replay_speed") return std::to_string(runtime.replay_speed);
        return default_value;
    }
    
    static bool get_config_bool(const std::string& key, bool default_value) {
        if (key == "market_data.loop_replay") return runtime.loop_replay;
        return default_value;
    }
    
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