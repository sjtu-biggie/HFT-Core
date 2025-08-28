#include "metrics_publisher.h"
#include "high_res_timer.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace hft {

MetricsPublisher::MetricsPublisher(const std::string& service_name, const std::string& endpoint)
    : service_name_(service_name), endpoint_(endpoint), running_(false) {
    // Truncate service name if too long
    if (service_name_.length() >= 32) {
        service_name_ = service_name_.substr(0, 31);
    }
}

MetricsPublisher::~MetricsPublisher() {
    stop();
}

bool MetricsPublisher::initialize() {
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        
        // Set socket options for better performance
        int sndhwm = 1000;
        publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        int linger = 0;
        publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        publisher_->bind(endpoint_);
        
        std::cout << "[MetricsPublisher] " << service_name_ << " bound to " << endpoint_ << std::endl;
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[MetricsPublisher] Failed to initialize: " << e.what() << std::endl;
        return false;
    }
}

void MetricsPublisher::start(int publish_interval_ms) {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    publish_thread_ = std::make_unique<std::thread>(&MetricsPublisher::publish_loop, this, publish_interval_ms);
    
    std::cout << "[MetricsPublisher] " << service_name_ << " started publishing every " 
              << publish_interval_ms << "ms" << std::endl;
}

void MetricsPublisher::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    if (publish_thread_ && publish_thread_->joinable()) {
        publish_thread_->join();
    }
    
    if (publisher_) {
        publisher_->close();
    }
    
    std::cout << "[MetricsPublisher] " << service_name_ << " stopped" << std::endl;
}

void MetricsPublisher::publish_loop(int interval_ms) {
    auto interval = std::chrono::milliseconds(interval_ms);
    
    while (running_.load()) {
        try {
            auto message_data = serialize_metrics();
            
            if (!message_data.empty()) {
                zmq::message_t message(message_data.size());
                std::memcpy(message.data(), message_data.data(), message_data.size());
                publisher_->send(message, zmq::send_flags::dontwait);
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {
                std::cerr << "[MetricsPublisher] Publish error: " << e.what() << std::endl;
            }
        }
        
        std::this_thread::sleep_for(interval);
    }
}

std::vector<uint8_t> MetricsPublisher::serialize_metrics() {
    auto& collector = MetricsCollector::instance();
    auto stats = collector.get_statistics();
    
    if (stats.empty()) {
        return {};
    }
    
    // Calculate message size
    size_t message_size = sizeof(MetricsMessage) + (stats.size() * sizeof(SerializedMetricEntry));
    std::vector<uint8_t> buffer(message_size);
    
    // Fill header
    MetricsMessage* header = reinterpret_cast<MetricsMessage*>(buffer.data());
    std::memset(header->service_name, 0, sizeof(header->service_name));
    std::strncpy(header->service_name, service_name_.c_str(), sizeof(header->service_name) - 1);
    header->timestamp_ns = HighResTimer::get_nanoseconds();
    header->metric_count = static_cast<uint32_t>(stats.size());
    
    // Fill metrics data
    SerializedMetricEntry* entries = reinterpret_cast<SerializedMetricEntry*>(buffer.data() + sizeof(MetricsMessage));
    size_t entry_idx = 0;
    
    for (const auto& [name, metric_stats] : stats) {
        SerializedMetricEntry& entry = entries[entry_idx++];
        
        std::memset(entry.name, 0, sizeof(entry.name));
        std::strncpy(entry.name, name.c_str(), sizeof(entry.name) - 1);
        entry.type = static_cast<uint32_t>(metric_stats.type);
        
        // Choose appropriate value based on metric type
        switch (metric_stats.type) {
            case MetricType::LATENCY:
                entry.value = metric_stats.p99; // Use P99 as representative value
                break;
            case MetricType::COUNTER:
                entry.value = metric_stats.count;
                break;
            case MetricType::GAUGE:
                entry.value = metric_stats.recent_values.empty() ? 0 : metric_stats.recent_values.back();
                break;
            case MetricType::HISTOGRAM:
                entry.value = metric_stats.p95; // Use P95 as representative value
                break;
        }
    }
    
    return buffer;
}

} // namespace hft