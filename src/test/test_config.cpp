#include "../common/config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>

using namespace hft;

void test_config_creation() {
    std::cout << "Testing config creation..." << std::endl;
    
    Config config;
    
    // Test default values
    assert(config.get_string(GlobalConfig::MARKET_DATA_ENDPOINT) == "tcp://localhost:5556");
    assert(config.get_int(GlobalConfig::CONTROL_API_PORT) == 8080);
    assert(config.get_bool(GlobalConfig::PAPER_TRADING) == true);
    
    std::cout << "✓ Config creation test passed" << std::endl;
}

void test_config_set_get() {
    std::cout << "Testing config set/get operations..." << std::endl;
    
    Config config;
    
    // Test string operations
    config.set_string("test.string", "hello world");
    assert(config.get_string("test.string") == "hello world");
    assert(config.get_string("nonexistent", "default") == "default");
    
    // Test integer operations
    config.set_int("test.int", 42);
    assert(config.get_int("test.int") == 42);
    assert(config.get_int("nonexistent.int", -1) == -1);
    
    // Test double operations
    config.set_double("test.double", 3.14159);
    assert(std::abs(config.get_double("test.double") - 3.14159) < 0.00001);
    assert(config.get_double("nonexistent.double", 2.71) == 2.71);
    
    // Test boolean operations
    config.set_bool("test.bool", true);
    assert(config.get_bool("test.bool") == true);
    config.set_bool("test.bool", false);
    assert(config.get_bool("test.bool") == false);
    assert(config.get_bool("nonexistent.bool", true) == true);
    
    std::cout << "✓ Config set/get test passed" << std::endl;
}

void test_config_file_operations() {
    std::cout << "Testing config file operations..." << std::endl;
    
    const std::string test_file = "test_config.conf";
    
    // Create and save config
    Config config;
    config.set_string("server.host", "localhost");
    config.set_int("server.port", 8080);
    config.set_double("trading.max_risk", 0.02);
    config.set_bool("logging.enabled", true);
    
    // Save to file
    assert(config.save_to_file(test_file) == true);
    
    // Create new config and load from file
    Config loaded_config;
    assert(loaded_config.load_from_file(test_file) == true);
    
    // Verify loaded values
    assert(loaded_config.get_string("server.host") == "localhost");
    assert(loaded_config.get_int("server.port") == 8080);
    assert(std::abs(loaded_config.get_double("trading.max_risk") - 0.02) < 0.00001);
    assert(loaded_config.get_bool("logging.enabled") == true);
    
    // Clean up
    std::filesystem::remove(test_file);
    
    std::cout << "✓ Config file operations test passed" << std::endl;
}

void test_config_keys() {
    std::cout << "Testing config key operations..." << std::endl;
    
    Config config;
    config.set_string("key1", "value1");
    config.set_int("key2", 123);
    config.set_bool("key3", true);
    
    // Test has_key
    assert(config.has_key("key1") == true);
    assert(config.has_key("key2") == true);
    assert(config.has_key("key3") == true);
    assert(config.has_key("nonexistent") == false);
    
    // Test get_all_keys
    auto keys = config.get_all_keys();
    assert(keys.size() >= 3);  // At least our 3 keys + defaults
    
    // Test clear
    config.clear();
    assert(config.has_key("key1") == false);
    assert(config.get_all_keys().empty() == true);
    
    std::cout << "✓ Config keys test passed" << std::endl;
}

void test_global_config() {
    std::cout << "Testing global config..." << std::endl;
    
    // Initialize global config
    GlobalConfig::instance().init();
    Config& global_config = GlobalConfig::instance().get();
    
    // Test default values through global config
    assert(global_config.get_string(GlobalConfig::MARKET_DATA_ENDPOINT) == "tcp://localhost:5556");
    assert(global_config.get_bool(GlobalConfig::TRADING_ENABLED) == false);
    
    // Modify and verify
    global_config.set_string("test.global", "global_value");
    assert(global_config.get_string("test.global") == "global_value");
    
    std::cout << "✓ Global config test passed" << std::endl;
}

void test_config_parsing() {
    std::cout << "Testing config file parsing..." << std::endl;
    
    const std::string test_file = "parse_test.conf";
    
    // Create test config file with various formats
    std::ofstream file(test_file);
    file << "# This is a comment\\n";
    file << "\\n";  // Empty line
    file << "simple.key=simple_value\\n";
    file << "spaced.key = value with spaces \\n";
    file << "number.key=42\\n";
    file << "bool.true=true\\n";
    file << "bool.false=false\\n";
    file << "bool.one=1\\n";
    file << "bool.zero=0\\n";
    file.close();
    
    // Load and test
    Config config;
    assert(config.load_from_file(test_file) == true);
    
    assert(config.get_string("simple.key") == "simple_value");
    assert(config.get_string("spaced.key") == "value with spaces");
    assert(config.get_int("number.key") == 42);
    assert(config.get_bool("bool.true") == true);
    assert(config.get_bool("bool.false") == false);
    assert(config.get_bool("bool.one") == true);
    assert(config.get_bool("bool.zero") == false);
    
    // Clean up
    std::filesystem::remove(test_file);
    
    std::cout << "✓ Config parsing test passed" << std::endl;
}

int main() {
    std::cout << "Running Configuration Unit Tests" << std::endl;
    std::cout << "===============================" << std::endl;
    
    try {
        test_config_creation();
        test_config_set_get();
        test_config_file_operations();
        test_config_keys();
        test_global_config();
        test_config_parsing();
        
        std::cout << "\n✅ All configuration tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}