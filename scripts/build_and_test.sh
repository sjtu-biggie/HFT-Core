#!/bin/bash

# HFT System Build and Test Script
# Phase 2 Implementation

set -e

echo "================================================"
echo "HFT System Build and Test Script"
echo "Phase 2 Implementation"
echo "================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
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

# Check if we're in the right directory
if [ ! -f "CMakeLists.txt" ]; then
    print_error "Please run this script from the HFT project root directory"
    exit 1
fi

# Check dependencies
print_status "Checking dependencies..."

check_dependency() {
    if command -v $1 &> /dev/null; then
        print_status "$1 found"
    else
        print_error "$1 not found. Please install it first."
        exit 1
    fi
}

check_dependency "cmake"
check_dependency "make"
check_dependency "g++"

# Check for optional dependencies
if pkg-config --exists libzmq; then
    print_status "ZeroMQ found"
else
    print_warning "ZeroMQ not found. Install with: sudo apt install libzmq3-dev"
fi

if pkg-config --exists liburing; then
    print_status "liburing found"
else
    print_warning "liburing not found. Install with: sudo apt install liburing-dev"
fi

# Create build directory
print_status "Setting up build directory..."
mkdir -p build
cd build

# Configure build
print_status "Configuring build..."
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
print_status "Building HFT system..."
make -j$(nproc)

if [ $? -eq 0 ]; then
    print_status "Build completed successfully"
else
    print_error "Build failed"
    exit 1
fi

# Check if ZeroMQ is available for testing
if pkg-config --exists libzmq; then
    # Run unit tests
    print_status "Running unit tests..."
    if [ -f "unit_tests" ]; then
        ./unit_tests
        if [ $? -eq 0 ]; then
            print_status "Unit tests passed"
        else
            print_warning "Some unit tests failed"
        fi
    else
        print_warning "Unit tests executable not found"
    fi
    
    # Offer to run integration test
    echo
    read -p "Run integration test? This will take 30 seconds (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        print_status "Starting integration test..."
        
        # Start mock data generator in background
        print_status "Starting mock data generator..."
        ./mock_data_generator 35 100 &
        MOCK_PID=$!
        
        # Give it time to start
        sleep 2
        
        # Run integration test
        print_status "Running integration test..."
        timeout 32s ./integration_test 30
        
        # Clean up
        if ps -p $MOCK_PID > /dev/null; then
            kill $MOCK_PID 2>/dev/null || true
        fi
        
        print_status "Integration test completed"
    fi
    
    # Show available executables
    echo
    print_status "Build artifacts created:"
    ls -la | grep -E "(market_data_handler|strategy_engine|order_gateway|position_risk_service|low_latency_logger|mock_data_generator|integration_test|unit_tests)" || true
    
    echo
    print_status "To run the full system:"
    echo "  1. Start services in separate terminals:"
    echo "     ./build/low_latency_logger"
    echo "     ./build/mock_data_generator 60 100"
    echo "     ./build/strategy_engine" 
    echo "     ./build/order_gateway"
    echo "     ./build/position_risk_service"
    echo
    echo "  2. Monitor with integration test:"
    echo "     ./build/integration_test 60"
    
else
    print_warning "ZeroMQ not available - skipping tests"
    print_warning "Install ZeroMQ with: sudo apt install libzmq3-dev"
fi

echo
print_status "Build and test script completed!"
print_status "Check README.md for detailed usage instructions."