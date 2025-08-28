#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include "../common/hft_metrics.h"
#include "../common/metrics_publisher.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>

namespace hft {

class MarketDataHandler {
public:
    MarketDataHandler();
    ~MarketDataHandler();
    
    // Initialize the handler with configuration
    bool initialize();
    
    // Start the market data processing
    void start();
    
    // Stop the market data processing
    void stop();
    
    // Check if handler is running
    bool is_running() const;

private:
    
    // ZeroMQ context and publisher socket
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> publisher_;
    
    // Processing control
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> processing_thread_;
    
    // Statistics
    std::atomic<uint64_t> messages_processed_;
    std::atomic<uint64_t> bytes_processed_;
    
    // Main processing loop
    void process_market_data();
    
    // DPDK-specific functions (proof-of-concept)
    bool initialize_dpdk();
    bool process_dpdk_packets();
    
    // Mock data generation for testing
    void generate_mock_data();
    
    // Publish market data message
    void publish_market_data(const MarketData& data);
    
    // Performance monitoring
    void log_statistics();
    
    Logger logger_;
    
    // HFT Metrics tracking
    ComponentThroughput throughput_tracker_;
    MetricsPublisher metrics_publisher_;
};

} // namespace hft