#include "control_api.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

static bool g_running = true;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main() {
    std::cout << "HFT Control API v2.0" << std::endl;
    std::cout << "====================" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Start the Control API
        start_control_api();
        
        std::cout << "Control API is running on localhost:8081" << std::endl;
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "  POST /api/start - Start trading" << std::endl;
        std::cout << "  POST /api/stop - Stop trading" << std::endl;
        std::cout << "  POST /api/emergency_stop - Emergency stop" << std::endl;
        std::cout << "  POST /api/liquidate - Liquidate all positions" << std::endl;
        std::cout << "  GET /api/status - Get system status" << std::endl;
        std::cout << "Authentication: X-API-Key header required" << std::endl;
        std::cout << "Press Ctrl+C to stop." << std::endl;
        
        // Main loop
        while (g_running && is_control_api_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Clean shutdown
        stop_control_api();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Control API shutdown complete." << std::endl;
    return 0;
}