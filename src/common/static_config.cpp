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
        filename,                           // Original path (e.g., "config/hft_config.conf")
        std::string("../") + filename,      // From build directory (e.g., "../config/hft_config.conf") 
        std::string("../../") + filename,   // From nested build dirs
        "/home/gyang197/project/hft/config/hft_config.conf"  // Absolute fallback
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
            static std::string market_data_endpoint_storage = value;
            runtime.market_data_endpoint = market_data_endpoint_storage.c_str();
        }
        else if (key == "logger.endpoint") {
            static std::string logger_endpoint_storage = value;
            runtime.logger_endpoint = logger_endpoint_storage.c_str();
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
        // Ignore unknown keys silently for forward compatibility
    }
    
    file.close();
    
    if (!validate_config()) {
        std::cerr << "[StaticConfig] Error: Invalid configuration after loading from " << filename << std::endl;
        return false;
    }
    
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
    oss << "    trading_enabled: " << (get_trading_enabled() ? "true" : "false") << "\n";
    oss << "    paper_trading: " << (get_paper_trading() ? "true" : "false") << "\n";
    oss << "    mock_data_enabled: " << (get_mock_data_enabled() ? "true" : "false") << "\n";
    oss << "    log_to_console: " << (get_log_to_console() ? "true" : "false") << "\n";
    
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