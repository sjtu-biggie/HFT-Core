#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include "../common/hft_metrics.h"
#include "../common/metrics_publisher.h"
#include "pcap_reader.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <random>

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
    
    // ZeroMQ context and sockets
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> publisher_;
    std::unique_ptr<zmq::socket_t> control_subscriber_;
    
    // Processing control
    std::atomic<bool> running_;
    std::atomic<bool> paused_;
    std::unique_ptr<std::thread> processing_thread_;
    std::unique_ptr<std::thread> control_thread_;
    
    // Statistics
    std::atomic<uint64_t> messages_processed_;
    std::atomic<uint64_t> bytes_processed_;
    
    // Main processing loop
    void process_market_data();
    
    // Control message processing
    void process_control_messages();
    void handle_control_command(const ControlCommand& command);
    
    // DPDK-specific functions (proof-of-concept)
    bool initialize_dpdk();
    bool process_dpdk_packets();
    
    // PCAP file processing
    bool initialize_pcap_reader();
    void process_pcap_data();
    
    // Mock data generation for testing
    void generate_mock_data();
    
    // Enhanced mock data with realistic price movements
    void generate_realistic_mock_data();
    
    // Market session and volatility helpers
    double get_market_session_volatility() const;
    double get_symbol_base_price(const std::string& symbol) const;
    
    // Publish market data message
    void publish_market_data(const MarketData& data);
    
    // Performance monitoring
    void log_statistics();
    
    Logger logger_;
    
    // HFT Metrics tracking
    ComponentThroughput throughput_tracker_;
    MetricsPublisher metrics_publisher_;
    
    // Enhanced mock data state
    std::unordered_map<std::string, double> symbol_prices_;
    std::unordered_map<std::string, double> symbol_volatilities_;
    std::mt19937 price_generator_;
    std::normal_distribution<double> price_change_dist_;
    std::chrono::steady_clock::time_point session_start_time_;
    
    // PCAP reader for market data replay
    std::unique_ptr<PCAPReader> pcap_reader_;
};

} // namespace hft