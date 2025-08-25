#include "low_latency_logger.h"
#include "../common/config.h"
#include <iostream>
#include <signal.h>
#include <thread>

using namespace hft;

static std::unique_ptr<LowLatencyLogger> g_logger;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_logger) {
        g_logger->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT Low-Latency Logger v1.0" << std::endl;
    std::cout << "============================" << std::endl;
    
    std::string config_file = (argc > 1) ? argv[1] : "config/hft_config.conf";
    GlobalConfig::instance().init(config_file);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_logger = std::make_unique<LowLatencyLogger>();
        
        if (!g_logger->initialize()) {
            std::cerr << "Failed to initialize Low-Latency Logger" << std::endl;
            return 1;
        }
        
        g_logger->start();
        
        std::cout << "Low-Latency Logger is running. Press Ctrl+C to stop." << std::endl;
        
        while (g_logger->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Low-Latency Logger shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}