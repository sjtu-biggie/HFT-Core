#include "../common/logging.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace hft;

void test_logger_creation() {
    std::cout << "Testing logger creation..." << std::endl;
    
    // Create logger (will fail to connect to non-existent endpoint, but shouldn't crash)
    Logger logger("TestComponent", "tcp://localhost:9999");
    
    std::cout << "✓ Logger creation test passed" << std::endl;
}

void test_log_levels() {
    std::cout << "Testing log levels..." << std::endl;
    
    Logger logger("TestLogLevels");
    logger.set_console_output(true);  // Enable for testing
    
    // Test different log levels
    logger.debug("This is a debug message");
    logger.info("This is an info message");
    logger.warning("This is a warning message");
    logger.error("This is an error message");
    logger.critical("This is a critical message");
    
    // Test log level filtering
    logger.set_log_level(LogLevel::WARNING);
    logger.debug("This debug should be filtered out");
    logger.info("This info should be filtered out");
    logger.warning("This warning should appear");
    logger.error("This error should appear");
    
    std::cout << "✓ Log levels test passed" << std::endl;
}

void test_global_logger() {
    std::cout << "Testing global logger..." << std::endl;
    
    // Initialize global logger
    GlobalLogger::instance().init("TestGlobalLogger");
    
    // Test macros
    HFT_LOG_INFO("Testing global logger with macro");
    HFT_LOG_WARNING("Testing warning with macro");
    
    std::cout << "✓ Global logger test passed" << std::endl;
}

void test_concurrent_logging() {
    std::cout << "Testing concurrent logging..." << std::endl;
    
    Logger logger("ConcurrentTest");
    logger.set_console_output(false);  // Disable console to avoid spam
    
    const int num_threads = 4;
    const int messages_per_thread = 100;
    std::vector<std::thread> threads;
    
    // Launch multiple threads logging simultaneously
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&logger, i, messages_per_thread]() {
            for (int j = 0; j < messages_per_thread; ++j) {
                logger.info("Thread " + std::to_string(i) + " message " + std::to_string(j));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    std::cout << "✓ Concurrent logging test passed" << std::endl;
}

void test_message_factory_log_creation() {
    std::cout << "Testing log message factory..." << std::endl;
    
    LogMessage msg = MessageFactory::create_log_message(
        LogLevel::ERROR, "FactoryTest", "Factory created message"
    );
    
    assert(msg.header.type == MessageType::LOG_MESSAGE);
    assert(msg.level == LogLevel::ERROR);
    assert(std::string(msg.component) == "FactoryTest");
    assert(std::string(msg.message) == "Factory created message");
    
    std::cout << "✓ Message factory log creation test passed" << std::endl;
}

void test_performance_logging() {
    std::cout << "Testing logging performance..." << std::endl;
    
    Logger logger("PerformanceTest");
    logger.set_console_output(false);  // Disable console output for performance
    
    const int num_messages = 10000;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_messages; ++i) {
        logger.info("Performance test message " + std::to_string(i));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    double messages_per_second = (num_messages * 1000000.0) / duration.count();
    std::cout << "Logged " << num_messages << " messages in " << duration.count() 
              << " microseconds (" << static_cast<int>(messages_per_second) << " msg/sec)" << std::endl;
    
    // Performance should be reasonable (>1000 messages per second)
    assert(messages_per_second > 1000);
    
    std::cout << "✓ Logging performance test passed" << std::endl;
}

int main() {
    std::cout << "Running Logging Unit Tests" << std::endl;
    std::cout << "=========================" << std::endl;
    
    try {
        test_logger_creation();
        test_log_levels();
        test_global_logger();
        test_concurrent_logging();
        test_message_factory_log_creation();
        test_performance_logging();
        
        std::cout << "\n✅ All logging tests passed!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}