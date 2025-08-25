#include "strategy_engine.h"
#include "../common/logging.h"
#include "../common/config.h"
#include <iostream>
#include <signal.h>
#include <thread>

using namespace hft;

static std::unique_ptr<StrategyEngine> g_engine;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_engine) {
        g_engine->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT Strategy Engine v1.0" << std::endl;
    std::cout << "=========================" << std::endl;
    
    // Initialize configuration
    std::string config_file = (argc > 1) ? argv[1] : "config/hft_config.conf";
    GlobalConfig::instance().init(config_file);
    
    // Initialize logging
    GlobalLogger::instance().init("StrategyEngine");
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_engine = std::make_unique<StrategyEngine>();
        
        if (!g_engine->initialize()) {
            std::cerr << "Failed to initialize Strategy Engine" << std::endl;
            return 1;
        }
        
        g_engine->start();
        
        std::cout << "Strategy Engine is running. Press Ctrl+C to stop." << std::endl;
        
        while (g_engine->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Strategy Engine shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}