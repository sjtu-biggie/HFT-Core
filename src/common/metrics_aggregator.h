#pragma once

#include "metrics_collector.h"
#include "metrics_publisher.h"
#include <zmq.hpp>
#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace hft {

// Service metrics with timestamp for staleness detection
struct ServiceMetrics {
    std::string service_name;
    uint64_t last_update_ns;
    std::unordered_map<std::string, MetricStats> metrics;
    bool is_online;
    
    ServiceMetrics() : last_update_ns(0), is_online(false) {}
};

// Aggregates metrics from multiple services via ZMQ
class MetricsAggregator {
public:
    explicit MetricsAggregator(const std::string& subscriber_endpoint = "tcp://localhost:5560");
    ~MetricsAggregator();
    
    bool initialize();
    void start();
    void stop();
    
    // Get aggregated metrics from all services
    std::unordered_map<std::string, MetricStats> get_all_metrics() const;
    std::vector<std::string> get_online_services() const;
    
    // Get metrics for a specific service
    std::unordered_map<std::string, MetricStats> get_service_metrics(const std::string& service_name) const;
    
    // Initialize all expected metrics with zero values
    void initialize_default_metrics();

private:
    std::string subscriber_endpoint_;
    
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> subscriber_;
    
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> subscribe_thread_;
    std::unique_ptr<std::thread> cleanup_thread_;
    
    // Thread-safe storage for aggregated metrics
    mutable std::mutex metrics_mutex_;
    std::unordered_map<std::string, ServiceMetrics> service_metrics_;
    
    // Default metric values to ensure all metrics are always available
    std::unordered_map<std::string, MetricStats> default_metrics_;
    
    void subscribe_loop();
    void cleanup_loop();
    void process_metrics_message(const std::vector<uint8_t>& data);
    void mark_service_offline(const std::string& service_name);
    
    // Service staleness detection (5 seconds timeout)
    static constexpr uint64_t SERVICE_TIMEOUT_NS = 5000000000ULL;
};

} // namespace hft