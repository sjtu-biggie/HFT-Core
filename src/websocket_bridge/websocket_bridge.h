#pragma once

#include <memory>

namespace hft {
    class WebSocketBridge;
}

// Global functions for service management
void start_websocket_bridge();
void stop_websocket_bridge();
bool is_websocket_bridge_running();

extern std::unique_ptr<hft::WebSocketBridge> g_bridge;