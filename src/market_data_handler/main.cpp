#include "market_data_handler.h"
#include "../common/logging.h"
#include "../common/config.h"
#include <iostream>
#include <signal.h>
#include <thread>

using namespace hft;

// Global handler for clean shutdown
static std::unique_ptr<MarketDataHandler> g_handler;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_handler) {
        g_handler->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT Market Data Handler v1.0" << std::endl;
    std::cout << "==============================" << std::endl;
    
    // Initialize configuration
    std::string config_file = (argc > 1) ? argv[1] : "config/hft_config.conf";
    GlobalConfig::instance().init(config_file);
    
    // Initialize logging
    GlobalLogger::instance().init("MarketDataHandler");
    
    // Set up signal handling for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Create and initialize handler
        g_handler = std::make_unique<MarketDataHandler>();
        
        if (!g_handler->initialize()) {
            std::cerr << "Failed to initialize Market Data Handler" << std::endl;
            return 1;
        }
        
        // Start processing
        g_handler->start();
        
        std::cout << "Market Data Handler is running. Press Ctrl+C to stop." << std::endl;
        
        // Wait for shutdown signal
        while (g_handler->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Market Data Handler shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}