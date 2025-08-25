#include "position_risk_service.h"
#include "../common/logging.h"
#include "../common/config.h"
#include <iostream>
#include <signal.h>
#include <thread>

using namespace hft;

static std::unique_ptr<PositionRiskService> g_service;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_service) {
        g_service->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT Position & Risk Service v1.0" << std::endl;
    std::cout << "=================================" << std::endl;
    
    std::string config_file = (argc > 1) ? argv[1] : "config/hft_config.conf";
    GlobalConfig::instance().init(config_file);
    GlobalLogger::instance().init("PositionRiskService");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_service = std::make_unique<PositionRiskService>();
        
        if (!g_service->initialize()) {
            std::cerr << "Failed to initialize Position & Risk Service" << std::endl;
            return 1;
        }
        
        g_service->start();
        
        std::cout << "Position & Risk Service is running. Press Ctrl+C to stop." << std::endl;
        
        while (g_service->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Position & Risk Service shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}