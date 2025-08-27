cmake_minimum_required(VERSION 3.20)
project(HFTSystem VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release for performance
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler-specific options for performance
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native -flto)
        add_link_options(-flto)
    endif()
endif()

# Find required packages
find_package(PkgConfig REQUIRED)

# Try to find ZeroMQ
find_path(ZMQ_INCLUDE_DIR zmq.h)
find_library(ZMQ_LIBRARY NAMES zmq libzmq)

if(NOT ZMQ_INCLUDE_DIR OR NOT ZMQ_LIBRARY)
    message(FATAL_ERROR "ZeroMQ not found. Please install libzmq3-dev")
endif()

# Try to find liburing
find_path(URING_INCLUDE_DIR liburing.h)
find_library(URING_LIBRARY NAMES uring liburing)

if(NOT URING_INCLUDE_DIR OR NOT URING_LIBRARY)
    message(WARNING "liburing not found. Install liburing-dev for io_uring support")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${ZMQ_INCLUDE_DIR})
if(URING_INCLUDE_DIR)
    include_directories(${URING_INCLUDE_DIR})
endif()

# Common library with shared code
add_library(hft_common STATIC
    src/common/message_types.cpp
    src/common/logging.cpp
    src/common/config.cpp
    src/common/static_config.cpp
    src/common/high_res_timer.cpp
    src/common/metrics_collector.cpp
    src/common/order_book.cpp
    src/common/simple_transport_demo.cpp
)

target_include_directories(hft_common PUBLIC src)
target_link_libraries(hft_common ${ZMQ_LIBRARY} pthread)

# Create alias for common library to match backtesting CMakeLists
add_library(common_lib ALIAS hft_common)

# Define executables for each service
set(SERVICES
    market_data_handler
    strategy_engine
    order_gateway
    position_risk_service
    low_latency_logger
    websocket_bridge
    control_api
)

# Create executables for each service
foreach(SERVICE ${SERVICES})
    add_executable(${SERVICE} src/${SERVICE}/main.cpp src/${SERVICE}/${SERVICE}.cpp)
    target_link_libraries(${SERVICE} hft_common ${ZMQ_LIBRARY} pthread)
    
    # Add io_uring support for logger
    if(SERVICE STREQUAL "low_latency_logger" AND URING_LIBRARY)
        target_link_libraries(${SERVICE} ${URING_LIBRARY})
        target_compile_definitions(${SERVICE} PRIVATE HAS_IO_URING=1)
    endif()
endforeach()

# Add backtesting subdirectory
add_subdirectory(src/backtesting)

# Create test executables
enable_testing()

# Mock data generator for testing
add_executable(mock_data_generator src/test/mock_data_generator.cpp)
target_link_libraries(mock_data_generator hft_common ${ZMQ_LIBRARY} pthread)

# Integration test
add_executable(integration_test src/test/integration_test.cpp)
target_link_libraries(integration_test hft_common ${ZMQ_LIBRARY} pthread)

# Unit tests for common components (create separate executables)
add_executable(test_message_types src/test/test_message_types.cpp)
target_link_libraries(test_message_types hft_common ${ZMQ_LIBRARY} pthread)

add_executable(test_logging src/test/test_logging.cpp)
target_link_libraries(test_logging hft_common ${ZMQ_LIBRARY} pthread)

add_executable(test_config src/test/test_config.cpp)
target_link_libraries(test_config hft_common ${ZMQ_LIBRARY} pthread)

# Find Google Test for backtesting tests
find_package(GTest QUIET)
if(GTest_FOUND)
    add_executable(test_backtesting_framework src/test/test_backtesting_framework.cpp)
    target_link_libraries(test_backtesting_framework 
        hft_common 
        historical_data_player 
        fill_simulator 
        data_downloader
        GTest::gtest 
        GTest::gtest_main
        ${ZMQ_LIBRARY} 
        pthread
    )
    add_test(NAME test_backtesting_framework COMMAND test_backtesting_framework)
else()
    message(WARNING "Google Test not found. Backtesting framework tests will not be built.")
endif()

# Add tests to CTest
add_test(NAME test_message_types COMMAND test_message_types)
add_test(NAME test_logging COMMAND test_logging)
add_test(NAME test_config COMMAND test_config)
add_test(NAME integration_test COMMAND integration_test)

# Install targets
install(TARGETS ${SERVICES} mock_data_generator
    RUNTIME DESTINATION bin
)

# Create directories for runtime
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/data)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/config)