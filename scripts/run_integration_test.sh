#!/bin/bash

# HFT System - Run Integration Test
# Automatically starts services, runs integration test, and cleans up

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
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

print_test() {
    echo -e "${PURPLE}[TEST]${NC} $1"
}

print_service() {
    echo -e "${BLUE}[SERVICE]${NC} $1"
}

# Global variables
SERVICE_PIDS=()
PID_FILE="/tmp/hft_integration_test.pids"
TEST_DURATION=3000
MOCK_DURATION=30  # Run mock data a bit longer than the test
MOCK_FREQUENCY=100
SERVICES_MODE="core"  # core or all

# Cleanup function
cleanup() {
    print_warning "Cleaning up services..."
    
    # Stop integration test if running
    if [ -n "$TEST_PID" ] && kill -0 $TEST_PID 2>/dev/null; then
        print_status "Stopping integration test..."
        kill $TEST_PID 2>/dev/null || true
        wait $TEST_PID 2>/dev/null || true
    fi
    
    # Use the stop_services.sh script if available
    if [ -f "scripts/stop_services.sh" ]; then
        ./scripts/stop_services.sh "$PID_FILE" 2>/dev/null || true
    else
        # Manual cleanup
        print_status "Stopping services manually..."
        if [ -f "$PID_FILE" ]; then
            while IFS=: read -r service_name pid; do
                if [ -n "$pid" ] && kill -0 $pid 2>/dev/null; then
                    print_status "Stopping $service_name (PID: $pid)"
                    kill $pid 2>/dev/null || true
                fi
            done < "$PID_FILE"
            rm -f "$PID_FILE"
        fi
    fi
    
    print_status "Cleanup completed"
}

# Set up signal handling
trap cleanup INT TERM EXIT

# Function to start a service
start_service() {
    local service_name=$1
    local executable=$2
    local args="${3:-}"
    local log_file="logs/hft_${service_name}.log"
    
    print_service "Starting $service_name..."
    
    mkdir -p logs
    
    if [ -n "$args" ]; then
        $executable $args > "$log_file" 2>&1 &
    else
        $executable > "$log_file" 2>&1 &
    fi
    
    local pid=$!
    echo "$service_name:$pid" >> "$PID_FILE"
    SERVICE_PIDS+=($pid)
    
    sleep 1
    
    if kill -0 $pid 2>/dev/null; then
        print_status "$service_name started (PID: $pid)"
        return 0
    else
        print_error "$service_name failed to start"
        return 1
    fi
}

# Function to wait for services to be ready
wait_for_services() {
    local ports=(5556 5557 5558 5559)
    local timeout=30
    local elapsed=0
    
    print_status "Waiting for services to be ready..."
    
    while [ $elapsed -lt $timeout ]; do
        local ready_ports=0
        
        for port in "${ports[@]}"; do
            if nc -z localhost $port 2>/dev/null; then
                ready_ports=$((ready_ports + 1))
            fi
        done
        
        if [ $ready_ports -ge 2 ]; then  # At least market data and one other service
            print_status "Services are ready (${ready_ports}/4 ports active)"
            return 0
        fi
        
        sleep 2
        elapsed=$((elapsed + 2))
    done
    
    print_warning "Timeout waiting for services (only found ports ready: $(netstat -ln | grep -E ':(5556|5557|5558|5559)' | wc -l))"
    return 1
}

# Parse command line arguments
show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --test-duration SECONDS    Integration test duration (default: 20)"
    echo "  -d, --mock-duration SECONDS    Mock data duration (default: 30)"
    echo "  -f, --frequency HZ              Mock data frequency (default: 100)"
    echo "  -s, --services MODE             Services to start: 'core' or 'all' (default: core)"
    echo "  -h, --help                      Show this help message"
    echo ""
    echo "Description:"
    echo "  Automatically starts HFT services, runs integration test, and cleans up."
    echo "  This script solves the issue where integration test fails because services"
    echo "  aren't running."
    echo ""
    echo "Example:"
    echo "  $0 -t 30 -f 50                 # 30 second test with 50Hz data"
    echo "  $0 -s all -t 120               # 2 minute test with all services"
    exit 0
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--test-duration)
            TEST_DURATION="$2"
            shift 2
            ;;
        -d|--mock-duration)
            MOCK_DURATION="$2"
            shift 2
            ;;
        -f|--frequency)
            MOCK_FREQUENCY="$2"
            shift 2
            ;;
        -s|--services)
            SERVICES_MODE="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            ;;
    esac
done

# Validate arguments
if [ "$SERVICES_MODE" != "core" ] && [ "$SERVICES_MODE" != "all" ]; then
    print_error "Services mode must be 'core' or 'all'"
    exit 1
fi

# Ensure mock data runs longer than the test
if [ $MOCK_DURATION -le $TEST_DURATION ]; then
    MOCK_DURATION=$((TEST_DURATION + 30))
    print_warning "Mock duration adjusted to ${MOCK_DURATION}s (test duration + 30s)"
fi

echo "================================================"
echo "HFT Integration Test Runner"
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

# Check executables exist
REQUIRED_EXECUTABLES=("integration_test" "low_latency_logger" "market_data_handler" "strategy_engine" "order_gateway" "position_risk_service")

if [ "$SERVICES_MODE" == "all" ]; then
    REQUIRED_EXECUTABLES+=("websocket_bridge" "control_api")
fi

for exe in "${REQUIRED_EXECUTABLES[@]}"; do
    if [ ! -f "$exe" ]; then
        print_error "Executable '$exe' not found. Please build the project first"
        exit 1
    fi
done

# Clean up any existing PID file
rm -f "$PID_FILE"

print_test "Configuration:"
print_test "  Test Duration: ${TEST_DURATION}s"
print_test "  Mock Duration: ${MOCK_DURATION}s" 
print_test "  Mock Frequency: ${MOCK_FREQUENCY}Hz"
print_test "  Services Mode: $SERVICES_MODE"
echo

# Start services
print_status "Starting HFT services for integration test..."

# Start logger first
start_service "low_latency_logger" "./low_latency_logger"
sleep 2

# Start additional services if "all" mode
if [ "$SERVICES_MODE" == "all" ]; then
    start_service "websocket_bridge" "./websocket_bridge"
    start_service "control_api" "./control_api"
    sleep 1
fi

# Start core trading services
# start_service "mock_data_generator" "./mock_data_generator" "$MOCK_DURATION $MOCK_FREQUENCY"
start_service "market_data_handler" "./market_data_handler"
sleep 3  # Give data generator more time to establish

start_service "strategy_engine" "./strategy_engine"
sleep 2

start_service "order_gateway" "./order_gateway"
sleep 2

start_service "position_risk_service" "./position_risk_service"
sleep 2

print_status "All services started, waiting for readiness..."

# Wait for services to be ready
if ! wait_for_services; then
    print_error "Services are not ready, but continuing with test..."
fi

# Run integration test
print_test "Starting integration test (${TEST_DURATION}s)..."
echo "======================================================"

# Start integration test in background so we can monitor it
./integration_test $TEST_DURATION &
TEST_PID=$!

# Monitor the test
test_exit_code=0
if wait $TEST_PID; then
    print_test "Integration test completed successfully!"
    test_exit_code=0
else
    test_exit_code=$?
    print_error "Integration test failed with exit code $test_exit_code"
fi

echo "======================================================"

# Show service status
print_status "Service Status Summary:"
if [ -f "$PID_FILE" ]; then
    while IFS=: read -r service_name pid; do
        if kill -0 $pid 2>/dev/null; then
            print_status "  ✓ $service_name is running"
        else
            print_warning "  ✗ $service_name has stopped"
        fi
    done < "$PID_FILE"
fi

# Show log files created
if [ -d "logs" ]; then
    log_count=$(ls logs/hft_*.log 2>/dev/null | wc -l)
    if [ $log_count -gt 0 ]; then
        print_status "Log files created: $log_count"
        print_status "View logs with: tail -f build/logs/hft_*.log"
    fi
fi

# Final status
echo
if [ $test_exit_code -eq 0 ]; then
    print_test "🎉 Integration test PASSED!"
    print_test "The HFT system is working correctly"
else
    print_test "❌ Integration test FAILED!"
    print_test "Check the logs for details: build/logs/hft_*.log"
fi

print_status "Services will be stopped automatically..."

# Cleanup will happen via the trap
exit $test_exit_code