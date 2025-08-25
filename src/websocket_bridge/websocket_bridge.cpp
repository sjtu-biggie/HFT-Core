#include <iostream>
#include <thread>
#include <signal.h>

static bool g_running = true;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_running = false;
}

int main() {
    std::cout << "HFT WebSocket Bridge v1.0 (Placeholder)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "WebSocket Bridge is running (placeholder). Press Ctrl+C to stop." << std::endl;
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "WebSocket Bridge shutdown complete." << std::endl;
    return 0;
}