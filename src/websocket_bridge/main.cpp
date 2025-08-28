#include "websocket_bridge.h"
#include "../common/hft_metrics.h"
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
    std::cout << "HFT WebSocket Bridge v2.0" << std::endl;
    std::cout << "=========================" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        // Initialize HFT metrics system
        std::cout << "Initializing HFT metrics system..." << std::endl;
        hft::initialize_hft_metrics();
        
        // Start the WebSocket bridge
        start_websocket_bridge();
        
        std::cout << "WebSocket Bridge is running. Press Ctrl+C to stop." << std::endl;
        
        // Main loop
        while (g_running && is_websocket_bridge_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Clean shutdown
        stop_websocket_bridge();
        
        // Shutdown metrics system
        std::cout << "Shutting down HFT metrics system..." << std::endl;
        hft::shutdown_hft_metrics();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "WebSocket Bridge shutdown complete." << std::endl;
    return 0;
}