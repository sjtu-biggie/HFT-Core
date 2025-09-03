#!/bin/bash

# HFT System - Start Core Services
# Starts the essential services needed for basic trading functionality

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# PID file for tracking processes
PID_FILE="/tmp/hft_core_services.pids"

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

# Function to check if port is available
check_port() {
    if lsof -Pi :$1 -sTCP:LISTEN -t >/dev/null 2>&1; then
        return 1  # Port is in use
    else
        return 0  # Port is available
    fi
}

# Function to start a service and track its PID
start_service() {
    local service_name=$1
    local executable=$2
    local args="${3:-}"
    local log_file="logs/hft_${service_name}.log"
    
    print_service "Starting $service_name..."
    
    # Create logs directory if it doesn't exist
    mkdir -p logs
    
    # Start the service in background
    if [ -n "$args" ]; then
        $executable $args > "$log_file" 2>&1 &
    else
        $executable > "$log_file" 2>&1 &
    fi
    
    local pid=$!
    
    # Store PID for later cleanup
    echo "$service_name:$pid" >> "$PID_FILE"
    
    # Wait a moment to check if service started successfully
    sleep 1
    
    if kill -0 $pid 2>/dev/null; then
        print_status "$service_name started successfully (PID: $pid)"
        print_status "Logs: $log_file"
    else
        print_error "$service_name failed to start"
        return 1
    fi
    
    return 0
}

# Cleanup function
cleanup() {
    print_warning "Interrupt received, stopping services..."
    if [ -f "$PID_FILE" ]; then
        ./scripts/stop_services.sh
    fi
    exit 0
}

# Set up signal handling
trap cleanup INT TERM

echo "================================================"
echo "HFT System - Starting Core Services"
echo "================================================"

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    print_error "Please run this script from the HFT project root directory"
    exit 1
fi

# Check if build directory exists
if [ ! -d "build" ]; then
    print_error "Build directory not found. Please run 'make -j16' first"
    exit 1
fi

# Change to build directory
cd build

# Check that executables exist
REQUIRED_EXECUTABLES=(
    "low_latency_logger"
    "market_data_handler"
    "strategy_engine"
    "order_gateway"
    "position_risk_service"
)

for exe in "${REQUIRED_EXECUTABLES[@]}"; do
    if [ ! -f "$exe" ]; then
        print_error "Executable '$exe' not found. Please build the project first"
        exit 1
    fi
done

# Check port availability
REQUIRED_PORTS=(5556 5557 5558 5559)
for port in "${REQUIRED_PORTS[@]}"; do
    if ! check_port $port; then
        print_warning "Port $port is already in use. You may need to stop existing services first"
    fi
done

# Clean up any existing PID file
rm -f "$PID_FILE"

# Parse command line arguments
MOCK_DURATION=300  # 5 minutes default
MOCK_FREQUENCY=100 # 100 Hz default

while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--duration)
            MOCK_DURATION="$2"
            shift 2
            ;;
        -f|--frequency)
            MOCK_FREQUENCY="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -d, --duration SECONDS   Mock data duration (default: 300)"
            echo "  -f, --frequency HZ       Mock data frequency (default: 100)"
            echo "  -h, --help              Show this help message"
            echo ""
            echo "Services started:"
            echo "  - Low Latency Logger"
            echo "  - Mock Data Generator"
            echo "  - Strategy Engine"
            echo "  - Order Gateway"
            echo "  - Position & Risk Service"
            echo ""
            echo "Use 'scripts/stop_services.sh' to stop all services"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

print_status "Starting core services with mock data for ${MOCK_DURATION}s at ${MOCK_FREQUENCY}Hz"
print_warning "Use Ctrl+C to stop all services, or run 'scripts/stop_services.sh'"
echo

# Start services in dependency order

# 1. Start Low Latency Logger first (other services may log to it)
start_service "low_latency_logger" "./low_latency_logger"

# Small delay to ensure logger is ready
sleep 2

# 2. Start Mock Data Generator (provides market data)
start_service "market_data_handler" "./market_data_handler" "$MOCK_DURATION $MOCK_FREQUENCY"

# Small delay to ensure data is flowing
sleep 2

# 3. Start Strategy Engine (consumes market data, produces signals)
start_service "strategy_engine" "./strategy_engine"

# Small delay to ensure strategy is ready
sleep 2

# 4. Start Order Gateway (consumes signals, produces executions)
start_service "order_gateway" "./order_gateway"

# Small delay to ensure gateway is ready
sleep 2

# 5. Start Position & Risk Service (consumes executions, tracks positions)
start_service "position_risk_service" "./position_risk_service"

echo
print_status "All core services started successfully!"
print_status "Services will run for approximately ${MOCK_DURATION} seconds"
echo
print_status "To monitor the system:"
echo "  ./integration_test 60"
echo
print_status "To view logs:"
echo "  tail -f logs/hft_*.log"
echo
print_status "To stop services:"
echo "  ./scripts/stop_services.sh"
echo

# Monitor services
print_status "Monitoring services... (Press Ctrl+C to stop)"

# Keep script running and monitor services
while true do
    sleep 10
    
    # Check if all services are still running
    services_running=0
    total_services=0
    
    if [ -f "$PID_FILE" ]; then
        while IFS=: read -r service_name pid; do
            total_services=$((total_services + 1))
            if kill -0 $pid 2>/dev/null; then
                services_running=$((services_running + 1))
            else
                print_warning "$service_name (PID: $pid) has stopped"
            fi
        done < "$PID_FILE"
    fi
    
    # If mock data generator finished, that's expected
    if [ $services_running -lt $total_services ] && [ $services_running -ge 4 ]; then
        print_status "$services_running/$total_services services running (mock data may have finished)"
    elif [ $services_running -lt $total_services ]; then
        print_warning "Some services have stopped. Check logs for details."
        break
    fi
done

# Clean exit
print_status "Core services monitoring completed"