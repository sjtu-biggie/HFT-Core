#include "order_gateway.h"
#include "../common/logging.h"
#include "../common/config.h"
#include <iostream>
#include <signal.h>
#include <thread>

using namespace hft;

static std::unique_ptr<OrderGateway> g_gateway;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_gateway) {
        g_gateway->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT Order Gateway v1.0" << std::endl;
    std::cout << "======================" << std::endl;
    
    std::string config_file = (argc > 1) ? argv[1] : "config/hft_config.conf";
    GlobalConfig::instance().init(config_file);
    GlobalLogger::instance().init("OrderGateway");
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_gateway = std::make_unique<OrderGateway>();
        
        if (!g_gateway->initialize()) {
            std::cerr << "Failed to initialize Order Gateway" << std::endl;
            return 1;
        }
        
        g_gateway->start();
        
        std::cout << "Order Gateway is running. Press Ctrl+C to stop." << std::endl;
        
        while (g_gateway->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Order Gateway shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}