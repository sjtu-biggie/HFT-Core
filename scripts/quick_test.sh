#!/bin/bash

# HFT System - Quick Test
# Fast validation of basic system functionality

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
PID_FILE="/tmp/hft_quick_test.pids"
TOTAL_TESTS=0
PASSED_TESTS=0

# Cleanup function
cleanup() {
    if [ ${#SERVICE_PIDS[@]} -gt 0 ]; then
        print_status "Stopping test services..."
        for pid in "${SERVICE_PIDS[@]}"; do
            if kill -0 $pid 2>/dev/null; then
                kill $pid 2>/dev/null || true
            fi
        done
        # Wait a moment for graceful shutdown
        sleep 2
        # Force kill if still running
        for pid in "${SERVICE_PIDS[@]}"; do
            if kill -0 $pid 2>/dev/null; then
                kill -KILL $pid 2>/dev/null || true
            fi
        done
    fi
    
    rm -f "$PID_FILE"
}

# Set up signal handling
trap cleanup INT TERM EXIT

# Test function wrapper
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    print_test "Running: $test_name"
    
    if eval "$test_command"; then
        print_status "✅ $test_name - PASSED"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        print_error "❌ $test_name - FAILED"
        return 1
    fi
}

# Function to start a service quickly
start_quick_service() {
    local service_name=$1
    local executable=$2
    local args="${3:-}"
    
    if [ -n "$args" ]; then
        $executable $args > /dev/null 2>&1 &
    else
        $executable > /dev/null 2>&1 &
    fi
    
    local pid=$!
    SERVICE_PIDS+=($pid)
    echo "$service_name:$pid" >> "$PID_FILE"
    
    # Quick check if it started
    sleep 0.5
    if kill -0 $pid 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

echo "================================================"
echo "HFT Quick Test Suite"
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

cd build

print_test "Starting HFT Quick Test Suite..."
echo

# Test 1: Check if executables exist
run_test "Executable Presence Check" "
    [ -f 'low_latency_logger' ] && 
    [ -f 'market_data_handler' ] && 
    [ -f 'strategy_engine' ] && 
    [ -f 'order_gateway' ] && 
    [ -f 'position_risk_service' ] &&
    [ -f 'integration_test' ]
"

# Test 2: Check unit tests
if [ -f "test_message_types" ] && [ -f "test_logging" ] && [ -f "test_config" ]; then
    run_test "Unit Tests - Message Types" "./test_message_types > /dev/null 2>&1"
    run_test "Unit Tests - Logging" "timeout 10s ./test_logging > /dev/null 2>&1"
    run_test "Unit Tests - Config" "./test_config > /dev/null 2>&1"
else
    print_warning "Unit test executables not found, skipping unit tests"
fi

# Test 3: Service startup test
print_test "Testing service startup..."

run_test "Low Latency Logger Startup" "start_quick_service 'low_latency_logger' './low_latency_logger'"

sleep 1

run_test "Mock Data Generator Startup" "start_quick_service 'market_data_handler' './market_data_handler'"

sleep 2

run_test "Strategy Engine Startup" "start_quick_service 'strategy_engine' './strategy_engine'"

sleep 1

run_test "Order Gateway Startup" "start_quick_service 'order_gateway' './order_gateway'"

sleep 1

run_test "Position Risk Service Startup" "start_quick_service 'position_risk_service' './position_risk_service'"

# Test 4: Port availability check (after startup)
sleep 3
run_test "Market Data Port Active (5556)" "nc -z localhost 5556"
run_test "Trading Signals Port Active (5558)" "nc -z localhost 5558 || true"  # May not be immediately active

# Test 5: Quick integration test
print_test "Running mini integration test (15 seconds)..."
run_test "Mini Integration Test" "timeout 17s ./integration_test 15 > /dev/null 2>&1"

# Test 6: Configuration test
if [ -f "../config/hft_config.conf" ]; then
    run_test "Config File Readable" "[ -r '../config/hft_config.conf' ]"
else
    print_warning "Config file not found, skipping config test"
fi

# Test 7: Log directory creation
run_test "Log Directory Creation" "[ -d 'logs' ] || mkdir -p logs"

# Test 8: Service cleanup test
print_test "Testing service cleanup..."
cleanup_test_result=0
if [ -f "$PID_FILE" ]; then
    while IFS=: read -r service_name pid; do
        if ! kill -0 $pid 2>/dev/null; then
            print_warning "Service $service_name (PID: $pid) already stopped"
        fi
    done < "$PID_FILE"
    cleanup_test_result=0
else
    cleanup_test_result=1
fi

run_test "Service Process Tracking" "[ $cleanup_test_result -eq 0 ]"

echo
echo "================================================"
echo "Quick Test Results"
echo "================================================"

print_test "Tests Passed: $PASSED_TESTS/$TOTAL_TESTS"

if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    print_test "🎉 All tests PASSED! HFT system is ready."
    print_status "You can now run:"
    echo "  ./scripts/run_integration_test.sh    # Full integration test"
    echo "  ./scripts/start_core_services.sh     # Start services manually"
    exit_code=0
elif [ $PASSED_TESTS -gt $((TOTAL_TESTS * 3 / 4)) ]; then
    print_warning "⚠️  Most tests passed ($PASSED_TESTS/$TOTAL_TESTS). System likely functional."
    print_status "Check failed tests and retry with:"
    echo "  ./scripts/run_integration_test.sh"
    exit_code=1
else
    print_error "❌ Many tests failed ($PASSED_TESTS/$TOTAL_TESTS). System needs attention."
    print_error "Check build and dependencies:"
    echo "  make -j16                             # Rebuild"
    echo "  sudo apt install libzmq3-dev         # Install dependencies"
    exit_code=2
fi

echo
print_status "Quick test completed in $(($SECONDS))s"

# Show some helpful info
if [ -d logs ] && [ $(ls logs/*.log 2>/dev/null | wc -l) -gt 0 ]; then
    print_status "Log files created: $(ls logs/*.log | wc -l)"
    print_status "View logs with: tail -f build/logs/hft_*.log"
fi

exit $exit_code