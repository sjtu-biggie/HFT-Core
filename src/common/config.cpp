#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

namespace hft {

Config::Config() {
    // Set default configuration values
    set_string(GlobalConfig::MARKET_DATA_ENDPOINT, "tcp://localhost:5556");
    set_string(GlobalConfig::LOGGER_ENDPOINT, "tcp://localhost:5555");
    set_int(GlobalConfig::CONTROL_API_PORT, 8080);
    set_int(GlobalConfig::WEBSOCKET_PORT, 8081);
    set_bool(GlobalConfig::ENABLE_DPDK, false);
    set_bool(GlobalConfig::ENABLE_IO_URING, false);
    set_string(GlobalConfig::LOG_LEVEL, "INFO");
    set_bool(GlobalConfig::LOG_TO_CONSOLE, true);
    set_bool(GlobalConfig::TRADING_ENABLED, false);
    set_bool(GlobalConfig::PAPER_TRADING, true);
}

Config::Config(const std::string& config_file) : Config() {
    load_from_file(config_file);
}

bool Config::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        auto [key, value] = parse_line(line);
        if (!key.empty() && !value.empty()) {
            config_data_[key] = value;
        }
    }
    
    return true;
}

bool Config::save_to_file(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# HFT System Configuration\n";
    file << "# Generated configuration file\n\n";
    
    for (const auto& [key, value] : config_data_) {
        file << key << "=" << value << "\n";
    }
    
    return true;
}

std::string Config::get_string(const std::string& key, const std::string& default_value) const {
    auto it = config_data_.find(key);
    return (it != config_data_.end()) ? it->second : default_value;
}

int Config::get_int(const std::string& key, int default_value) const {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            // Fall through to default
        }
    }
    return default_value;
}

double Config::get_double(const std::string& key, double default_value) const {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        try {
            return std::stod(it->second);
        } catch (const std::exception&) {
            // Fall through to default
        }
    }
    return default_value;
}

bool Config::get_bool(const std::string& key, bool default_value) const {
    auto it = config_data_.find(key);
    if (it != config_data_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes" || value == "on";
    }
    return default_value;
}

void Config::set_string(const std::string& key, const std::string& value) {
    config_data_[key] = value;
}

void Config::set_int(const std::string& key, int value) {
    config_data_[key] = std::to_string(value);
}

void Config::set_double(const std::string& key, double value) {
    config_data_[key] = std::to_string(value);
}

void Config::set_bool(const std::string& key, bool value) {
    config_data_[key] = value ? "true" : "false";
}

bool Config::has_key(const std::string& key) const {
    return config_data_.find(key) != config_data_.end();
}

std::vector<std::string> Config::get_all_keys() const {
    std::vector<std::string> keys;
    keys.reserve(config_data_.size());
    for (const auto& [key, value] : config_data_) {
        keys.push_back(key);
    }
    return keys;
}

void Config::clear() {
    config_data_.clear();
}

std::string Config::trim(const std::string& str) const {
    const std::string whitespace = " \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

std::pair<std::string, std::string> Config::parse_line(const std::string& line) const {
    size_t eq_pos = line.find('=');
    if (eq_pos == std::string::npos) {
        return {"", ""};
    }
    
    std::string key = trim(line.substr(0, eq_pos));
    std::string value = trim(line.substr(eq_pos + 1));
    
    return {key, value};
}

// Singleton implementation
GlobalConfig& GlobalConfig::instance() {
    static GlobalConfig instance;
    return instance;
}

void GlobalConfig::init(const std::string& config_file) {
    if (config_file.empty()) {
        config_ = std::make_unique<Config>();
    } else {
        config_ = std::make_unique<Config>(config_file);
    }
}

Config& GlobalConfig::get() {
    if (!config_) {
        config_ = std::make_unique<Config>();
    }
    return *config_;
}

} // namespace hft