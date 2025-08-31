#!/bin/bash

# HFT System - Stop Services
# Gracefully stops all HFT services

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_service() {
    echo -e "${BLUE}[SERVICE]${NC} $1"
}

# Function to gracefully stop a process
stop_process() {
    local service_name=$1
    local pid=$2
    
    if kill -0 $pid 2>/dev/null; then
        print_service "Stopping $service_name (PID: $pid)..."
        
        # Try graceful shutdown first
        kill -TERM $pid 2>/dev/null || true
        
        # Wait up to 5 seconds for graceful shutdown
        local count=0
        while kill -0 $pid 2>/dev/null && [ $count -lt 5 ]; do
            sleep 1
            count=$((count + 1))
        done
        
        # If still running, force kill
        if kill -0 $pid 2>/dev/null; then
            print_warning "Force killing $service_name (PID: $pid)"
            kill -KILL $pid 2>/dev/null || true
            sleep 1
        fi
        
        # Verify it's stopped
        if kill -0 $pid 2>/dev/null; then
            print_error "Failed to stop $service_name (PID: $pid)"
            return 1
        else
            print_status "$service_name stopped successfully"
            return 0
        fi
    else
        print_warning "$service_name (PID: $pid) was not running"
        return 0
    fi
}

# Function to stop processes by name (fallback)
stop_by_name() {
    local process_name=$1
    local pids=$(pgrep -f "$process_name" 2>/dev/null || true)
    
    if [ -n "$pids" ]; then
        print_warning "Found running $process_name processes: $pids"
        for pid in $pids; do
            stop_process "$process_name" "$pid"
        done
    fi
}

echo "================================================"
echo "HFT System - Stopping Services"
echo "================================================"

# Determine PID file to use
PID_FILE=""
if [ -n "$1" ]; then
    # Use specified PID file
    PID_FILE="$1"
elif [ -f "/tmp/hft_all_services.pids" ]; then
    # Use all services PID file
    PID_FILE="/tmp/hft_all_services.pids"
elif [ -f "/tmp/hft_core_services.pids" ]; then
    # Use core services PID file
    PID_FILE="/tmp/hft_core_services.pids"
else
    print_warning "No PID files found, searching for running HFT processes..."
fi

services_stopped=0
total_services=0

# Stop services from PID file if it exists
if [ -n "$PID_FILE" ] && [ -f "$PID_FILE" ]; then
    print_status "Using PID file: $PID_FILE"
    
    # Read PID file and stop services
    while IFS=: read -r service_name pid; do
        if [ -n "$service_name" ] && [ -n "$pid" ]; then
            total_services=$((total_services + 1))
            if stop_process "$service_name" "$pid"; then
                services_stopped=$((services_stopped + 1))
            fi
        fi
    done < "$PID_FILE"
    
    # Clean up PID file
    rm -f "$PID_FILE"
    print_status "Cleaned up PID file: $PID_FILE"
    
else
    print_warning "No PID file found, attempting to find and stop HFT processes by name..."
    
    # List of HFT service names to search for
    HFT_SERVICES=(
        "low_latency_logger"
        "market_data_handler" 
        "strategy_engine"
        "order_gateway"
        "position_risk_service"
        "websocket_bridge"
        "control_api"
        "integration_test"
    )
    
    for service in "${HFT_SERVICES[@]}"; do
        stop_by_name "$service"
    done
fi

# Additional cleanup - check for any remaining processes
print_status "Checking for remaining HFT processes..."

# Look for processes containing 'hft' or our service names
remaining_pids=$(pgrep -f "(hft|market_data_handler|strategy_engine|order_gateway|position_risk|low_latency_logger|market_data_handler|websocket_bridge|control_api|integration_test)" 2>/dev/null || true)

if [ -n "$remaining_pids" ]; then
    print_warning "Found remaining HFT-related processes:"
    for pid in $remaining_pids; do
        process_info=$(ps -p $pid -o pid,ppid,cmd --no-headers 2>/dev/null || echo "Process not found")
        echo "  PID $pid: $process_info"
    done
    
    echo
    read -p "Stop these processes? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        for pid in $remaining_pids; do
            stop_process "remaining_process" "$pid"
        done
    fi
fi

# Clean up any remaining PID files
for pid_file in /tmp/hft_*.pids; do
    if [ -f "$pid_file" ]; then
        print_status "Cleaning up: $pid_file"
        rm -f "$pid_file"
    fi
done

# Port cleanup check
print_status "Checking if HFT ports are free..."
HFT_PORTS=(5556 5557 5558 5559 8080 8081)
ports_in_use=()

for port in "${HFT_PORTS[@]}"; do
    if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1; then
        port_info=$(lsof -Pi :$port -sTCP:LISTEN 2>/dev/null || echo "Unknown process")
        ports_in_use+=("$port ($port_info)")
    fi
done

if [ ${#ports_in_use[@]} -gt 0 ]; then
    print_warning "Some HFT ports are still in use:"
    for port_info in "${ports_in_use[@]}"; do
        echo "  Port $port_info"
    done
    print_warning "You may need to manually stop these processes"
else
    print_status "All HFT ports are now free"
fi

echo
if [ $total_services -gt 0 ]; then
    print_status "Summary: $services_stopped/$total_services services stopped successfully"
else
    print_status "Service cleanup completed"
fi

print_status "HFT services shutdown complete"
echo

# Optional: Show system status
if command -v pgrep >/dev/null 2>&1; then
    remaining=$(pgrep -f "(hft|market_data|strategy_engine|order_gateway|position_risk|low_latency_logger|market_data_handler|websocket_bridge|control_api)" 2>/dev/null | wc -l)
    if [ $remaining -gt 0 ]; then
        print_warning "$remaining HFT-related processes may still be running"
        print_status "Use 'ps aux | grep hft' to investigate further"
    else
        print_status "No HFT processes detected running"
    fi
fi