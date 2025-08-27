#pragma once

#include <memory>

namespace hft {
    class ControlAPI;
}

// Global functions for service management
void start_control_api();
void stop_control_api();
bool is_control_api_running();

extern std::unique_ptr<hft::ControlAPI> g_control_api;