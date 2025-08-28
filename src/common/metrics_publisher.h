#pragma once

#include "metrics_collector.h"
#include <zmq.hpp>
#include <string>
#include <atomic>
#include <thread>
#include <memory>
#include <chrono>

namespace hft {

// Metrics message for inter-process communication
struct MetricsMessage {
    char service_name[32];
    uint64_t timestamp_ns;
    uint32_t metric_count;
    // Variable-length data follows: SerializedMetricEntry entries
} __attribute__((packed));

// Individual metric entry in the message
struct SerializedMetricEntry {
    char name[64];
    uint64_t value;
    uint32_t type; // MetricType enum value
} __attribute__((packed));

// Publisher for distributing metrics to central aggregator
class MetricsPublisher {
public:
    explicit MetricsPublisher(const std::string& service_name, 
                             const std::string& endpoint = "tcp://*:5560");
    ~MetricsPublisher();
    
    bool initialize();
    void start(int publish_interval_ms = 2000); // Default 2 seconds
    void stop();
    
private:
    std::string service_name_;
    std::string endpoint_;
    
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> publisher_;
    
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> publish_thread_;
    
    void publish_loop(int interval_ms);
    std::vector<uint8_t> serialize_metrics();
};

} // namespace hft