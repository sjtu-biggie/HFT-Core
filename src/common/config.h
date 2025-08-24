#pragma once

#include <string>
#include <unordered_map>
#include <memory>

namespace hft {

class Config {
public:
    Config();
    explicit Config(const std::string& config_file);
    
    // Load configuration from file
    bool load_from_file(const std::string& filename);
    
    // Save configuration to file
    bool save_to_file(const std::string& filename) const;
    
    // Get configuration values with type conversion
    std::string get_string(const std::string& key, const std::string& default_value = "") const;
    int get_int(const std::string& key, int default_value = 0) const;
    double get_double(const std::string& key, double default_value = 0.0) const;
    bool get_bool(const std::string& key, bool default_value = false) const;
    
    // Set configuration values
    void set_string(const std::string& key, const std::string& value);
    void set_int(const std::string& key, int value);
    void set_double(const std::string& key, double value);
    void set_bool(const std::string& key, bool value);
    
    // Check if key exists
    bool has_key(const std::string& key) const;
    
    // Get all keys
    std::vector<std::string> get_all_keys() const;
    
    // Clear all configuration
    void clear();

private:
    std::unordered_map<std::string, std::string> config_data_;
    
    std::string trim(const std::string& str) const;
    std::pair<std::string, std::string> parse_line(const std::string& line) const;
};

// Singleton configuration for global access
class GlobalConfig {
public:
    static GlobalConfig& instance();
    void init(const std::string& config_file = "");
    Config& get();
    
    // Commonly used configuration keys
    static constexpr const char* MARKET_DATA_ENDPOINT = "market_data.endpoint";
    static constexpr const char* LOGGER_ENDPOINT = "logger.endpoint";
    static constexpr const char* CONTROL_API_PORT = "control_api.port";
    static constexpr const char* WEBSOCKET_PORT = "websocket.port";
    static constexpr const char* ENABLE_DPDK = "market_data.enable_dpdk";
    static constexpr const char* ENABLE_IO_URING = "logger.enable_io_uring";
    static constexpr const char* LOG_LEVEL = "logging.level";
    static constexpr const char* LOG_TO_CONSOLE = "logging.console";
    static constexpr const char* TRADING_ENABLED = "trading.enabled";
    static constexpr const char* PAPER_TRADING = "trading.paper_mode";

private:
    GlobalConfig() = default;
    std::unique_ptr<Config> config_;
};

} // namespace hft