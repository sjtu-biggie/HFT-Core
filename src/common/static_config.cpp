#include "static_config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

namespace hft {

// Initialize static runtime overrides with compile-time defaults
StaticConfig::RuntimeOverrides StaticConfig::runtime = {};

bool StaticConfig::load_from_file(const char* filename) {
    // Try multiple possible paths to make it always succeed
    std::vector<std::string> paths_to_try = {
        filename,                           // Original path (e.g., "config/hft_config_override.conf")
        std::string("../") + filename,      // From build directory (e.g., "../config/hft_config_override.conf") 
        std::string("../../") + filename,   // From nested build dirs
        "/home/gyang197/project/hft/config/hft_config_override.conf"  // Absolute fallback
    };
    
    std::ifstream file;
    std::string successful_path;
    
    for (const auto& path : paths_to_try) {
        file.open(path);
        if (file.is_open()) {
            successful_path = path;
            break;
        }
        file.clear(); // Clear any error flags before trying next path
    }
    
    if (!file.is_open()) {
        std::cerr << "[StaticConfig] Warning: Could not open config file at any of these paths:" << std::endl;
        for (const auto& path : paths_to_try) {
            std::cerr << "[StaticConfig]   - " << path << std::endl;
        }
        std::cerr << "[StaticConfig] Using compile-time defaults" << std::endl;
        return false;
    }
    
    std::cout << "[StaticConfig] Found config file at: " << successful_path << std::endl;
    
    std::string line;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Find key=value separator
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, equals_pos);
        std::string value = line.substr(equals_pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Parse configuration values using constexpr string comparisons where possible
        if (key == "market_data.endpoint") {
            // For endpoints, we need to store the string (allocation required)
            runtime.market_data_endpoint_storage = value;
        }
        else if (key == "logger.endpoint") {
            runtime.logger_endpoint_storage = value;
        }
        else if (key == "signals.endpoint") {
            runtime.signals_endpoint_storage = value;
        }
        else if (key == "executions.endpoint") {
            runtime.executions_endpoint_storage = value;
        }
        else if (key == "positions.endpoint") {
            runtime.positions_endpoint_storage = value;
        }
        else if (key == "control.endpoint") {
            runtime.control_endpoint_storage = value;
        }
        else if (key == "websocket.endpoint") {
            runtime.websocket_endpoint_storage = value;
        }
        else if (key == "market_data.enable_dpdk") {
            runtime.enable_dpdk = (value == "true");
        }
        else if (key == "market_data.source") {
            runtime.market_data_source = value;
        }
        else if (key == "logger.enable_io_uring") {
            runtime.enable_io_uring = (value == "true");
        }
        else if (key == "trading.enabled") {
            runtime.trading_enabled = (value == "true");
        }
        else if (key == "trading.paper_mode") {
            runtime.paper_trading = (value == "true");
        }
        else if (key == "trading.mode") {
            runtime.trading_mode = value;
        }
        else if (key == "trading.use_websocket_updates") {
            runtime.use_websocket_updates = (value == "true");
        }
        else if (key == "mock_data.enabled") {
            runtime.mock_data_enabled = (value == "true");
        }
        else if (key == "logging.console") {
            runtime.log_to_console = (value == "true");
        }
        else if (key == "logging.level") {
            runtime.log_level = get_log_level_from_string(value.c_str());
        }
        else if (key == "mock_data.frequency_hz") {
            runtime.mock_data_frequency_hz = std::stoi(value);
        }
        else if (key == "mock_data.symbols") {
            // Parse comma-separated symbol list
            runtime.symbols.clear();
            std::istringstream iss(value);
            std::string symbol;
            while (std::getline(iss, symbol, ',')) {
                // Trim whitespace from symbol
                symbol.erase(0, symbol.find_first_not_of(" \t"));
                symbol.erase(symbol.find_last_not_of(" \t") + 1);
                if (!symbol.empty()) {
                    runtime.symbols.push_back(symbol);
                }
            }
        }
        else if (key == "risk.max_position_value") {
            runtime.max_position_value = std::stod(value);
        }
        else if (key == "risk.max_daily_loss") {
            runtime.max_daily_loss = std::stod(value);
        }
        else if (key == "risk.position_limit_per_symbol") {
            runtime.position_limit_per_symbol = std::stoi(value);
        }
        else if (key == "strategy.momentum.threshold") {
            runtime.momentum_threshold = std::stod(value);
        }
        else if (key == "strategy.momentum.min_signal_interval_ms") {
            runtime.min_signal_interval_ms = std::stoi(value);
        }
        // Alpaca configuration
        else if (key == "alpaca.api_key") {
            runtime.alpaca_api_key = value;
        }
        else if (key == "alpaca.secret_key") {
            runtime.alpaca_secret_key = value;
        }
        else if (key == "alpaca.paper_trading") {
            runtime.alpaca_paper_trading = (value == "true");
        }
        else if (key == "alpaca.websocket_feed") {
            runtime.alpaca_websocket_feed = value;
        }
        else if (key == "alpaca.websocket_url") {
            runtime.alpaca_websocket_url = value;
        }
        else if (key == "alpaca.websocket_host") {
            runtime.alpaca_websocket_host = value;
        }
        // Alpaca trading configuration
        else if (key == "alpaca.paper_base_url") {
            runtime.alpaca_paper_base_url = value;
        }
        else if (key == "alpaca.live_base_url") {
            runtime.alpaca_live_base_url = value;
        }
        else if (key == "alpaca.trade_websocket_url") {
            runtime.alpaca_trade_websocket_url = value;
        }
        else if (key == "alpaca.trade_websocket_host") {
            runtime.alpaca_trade_websocket_host = value;
        }
        else if (key == "alpaca.max_symbols_per_request") {
            runtime.alpaca_max_symbols_per_request = std::stoi(value);
        }
        else if (key == "alpaca.max_message_size_kb") {
            runtime.alpaca_max_message_size_kb = std::stoi(value);
        }
        else if (key == "alpaca.reconnect_interval_seconds") {
            runtime.alpaca_reconnect_interval_seconds = std::stoi(value);
        }
        else if (key == "alpaca.max_reconnect_attempts") {
            runtime.alpaca_max_reconnect_attempts = std::stoi(value);
        }
        else if (key == "alpaca.auth_timeout_seconds") {
            runtime.alpaca_auth_timeout_seconds = std::stoi(value);
        }
        else if (key == "alpaca.circuit_breaker_failures") {
            runtime.alpaca_circuit_breaker_failures = std::stoi(value);
        }
        else if (key == "alpaca.circuit_breaker_timeout_minutes") {
            runtime.alpaca_circuit_breaker_timeout_minutes = std::stoi(value);
        }
        else if (key == "websocket.port") {
            runtime.websocket_port = std::stoi(value);
            if (runtime.websocket_endpoint_storage.empty()) {
                runtime.websocket_endpoint_storage = "tcp://localhost:" + std::to_string(runtime.websocket_port);
            }
        }
        else if (key == "market_data.port") {
            runtime.market_data_port = std::stoi(value);
            if (runtime.market_data_endpoint_storage.empty()) {
                runtime.market_data_endpoint_storage = "tcp://localhost:" + std::to_string(runtime.market_data_port);
            }
        }
        else if (key == "logger.port") {
            runtime.logger_port = std::stoi(value);
            if (runtime.logger_endpoint_storage.empty()) {
                runtime.logger_endpoint_storage = "tcp://localhost:" + std::to_string(runtime.logger_port);
            }
        }
        else if (key == "signals.port") {
            runtime.signals_port = std::stoi(value);
            if (runtime.signals_endpoint_storage.empty()) {
                runtime.signals_endpoint_storage = "tcp://localhost:" + std::to_string(runtime.signals_port);
            }
        }
        else if (key == "executions.port") {
            runtime.executions_port = std::stoi(value);
            if (runtime.executions_endpoint_storage.empty()) {
                runtime.executions_endpoint_storage = "tcp://localhost:" + std::to_string(runtime.executions_port);
            }
        }
        else if (key == "positions.port") {
            runtime.positions_port = std::stoi(value);
            if (runtime.positions_endpoint_storage.empty()) {
                runtime.positions_endpoint_storage = "tcp://localhost:" + std::to_string(runtime.positions_port);
            }
        }
        // ZMQ parameters
        // Timing parameters (many exist in config but aren't loaded)
        else if (key == "timing.poll_timeout_ms") {
            runtime.poll_timeout_ms = std::stoi(value);
        }
        else if (key == "timing.stats_interval_seconds") {
            runtime.stats_interval_seconds = std::stoi(value);
        }
        else if (key == "timing.control_poll_interval_ms") {
            runtime.control_poll_interval_ms = std::stoi(value);
        }
        else if (key == "timing.processing_sleep_microseconds") {
            runtime.processing_sleep_microseconds = std::stoi(value);
        }
        else if (key == "timing.fast_processing_sleep_microseconds") {
            runtime.fast_processing_sleep_microseconds = std::stoi(value);
        }
        else if (key == "timing.order_execution_min_delay_ms") {
            runtime.order_execution_min_delay_ms = std::stoi(value);
        }
        else if (key == "timing.order_execution_max_delay_ms") {
            runtime.order_execution_max_delay_ms = std::stoi(value);
        }
        else if (key == "timing.metrics_update_interval_seconds") {
            runtime.metrics_update_interval_seconds = std::stoi(value);
        }
        else if (key == "timing.metrics_publisher_interval_ms") {
            runtime.metrics_publisher_interval_ms = std::stoi(value);
        }
        // Mock data generation parameters
        else if (key == "mock_data.price_change_volatility") {
            runtime.price_change_volatility = std::stod(value);
        }
        else if (key == "mock_data.min_price_multiplier") {
            runtime.min_price_multiplier = std::stod(value);
        }
        else if (key == "mock_data.max_price_multiplier") {
            runtime.max_price_multiplier = std::stod(value);
        }
        else if (key == "mock_data.base_spread_basis_points") {
            runtime.base_spread_basis_points = std::stod(value);
        }
        else if (key == "mock_data.min_volume") {
            runtime.min_volume = std::stoi(value);
        }
        else if (key == "mock_data.max_volume") {
            runtime.max_volume = std::stoi(value);
        }
        else if (key == "mock_data.min_last_size") {
            runtime.min_last_size = std::stoi(value);
        }
        else if (key == "mock_data.max_last_size") {
            runtime.max_last_size = std::stoi(value);
        }
        // Symbol prices (from config file)
        else if (key.substr(0, 14) == "symbol_prices.") {
            std::string symbol = key.substr(14);
            runtime.symbol_base_prices[symbol] = std::stod(value);
        }
        // Symbol volatilities (from config file)
        else if (key.substr(0, 19) == "symbol_volatilities.") {
            std::string symbol = key.substr(19);
            runtime.symbol_volatilities[symbol] = std::stod(value);
        }
        // Metrics ports
        else if (key == "metrics.aggregator_port") {
            runtime.metrics_aggregator_port = std::stoi(value);
        }
        else if (key == "metrics.control_commands_port") {
            runtime.control_commands_port = std::stoi(value);
        }
        // Ignore unknown keys silently for forward compatibility
    }
    
    file.close();
    std::cout << "[StaticConfig] Successfully loaded configuration from " << filename << std::endl;
    return true;
}

std::string StaticConfig::to_string() {
    std::ostringstream oss;
    
    oss << "StaticConfig {\n";
    oss << "  Endpoints:\n";
    oss << "    market_data: " << get_market_data_endpoint() << "\n";
    oss << "    logger: " << get_logger_endpoint() << "\n";
    oss << "    signals: " << get_signals_endpoint() << "\n";
    oss << "    executions: " << get_executions_endpoint() << "\n";
    oss << "    positions: " << get_positions_endpoint() << "\n";
    
    oss << "  Features:\n";
    oss << "    enable_dpdk: " << (get_enable_dpdk() ? "true" : "false") << "\n";
    oss << "    enable_io_uring: " << (get_enable_io_uring() ? "true" : "false") << "\n";
    oss << "    mock_data_enabled: " << (get_mock_data_enabled() ? "true" : "false") << "\n";
    
    oss << "  Parameters:\n";
    oss << "    log_level: " << get_log_level() << "\n";
    oss << "    mock_data_frequency_hz: " << get_mock_data_frequency_hz() << "\n";
    oss << "    max_position_value: " << get_max_position_value() << "\n";
    oss << "    max_daily_loss: " << get_max_daily_loss() << "\n";
    oss << "    position_limit_per_symbol: " << get_position_limit_per_symbol() << "\n";
    oss << "    momentum_threshold: " << get_momentum_threshold() << "\n";
    oss << "    min_signal_interval_ms: " << get_min_signal_interval_ms() << "\n";
    
    oss << "  Default Symbols (" << get_symbol_count() << "):\n    ";
    for (size_t i = 0; DEFAULT_SYMBOLS[i] != nullptr; ++i) {
        if (i > 0) oss << ", ";
        oss << DEFAULT_SYMBOLS[i];
    }
    oss << "\n";
    
    oss << "}";
    return oss.str();
}

} // namespace hft