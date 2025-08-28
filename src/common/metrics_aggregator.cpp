#include "metrics_aggregator.h"
#include "hft_metrics.h"
#include "high_res_timer.h"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace hft {

MetricsAggregator::MetricsAggregator(const std::string& subscriber_endpoint)
    : subscriber_endpoint_(subscriber_endpoint), running_(false) {
}

MetricsAggregator::~MetricsAggregator() {
    stop();
}

bool MetricsAggregator::initialize() {
    try {
        context_ = std::make_unique<zmq::context_t>(1);
        subscriber_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        
        // Subscribe to all messages
        subscriber_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
        
        // Set socket options
        int rcvhwm = 1000;
        subscriber_->setsockopt(ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        int rcvtimeo = 1000; // 1 second timeout
        subscriber_->setsockopt(ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
        
        // Connect to multiple service endpoints
        std::vector<std::string> endpoints = {
            "tcp://localhost:5561", // Strategy Engine
            "tcp://localhost:5562", // Market Data Handler  
            "tcp://localhost:5563", // Order Gateway
            "tcp://localhost:5564"  // Position Risk Service
        };
        
        for (const auto& endpoint : endpoints) {
            try {
                subscriber_->connect(endpoint);
                std::cout << "[MetricsAggregator] Connected to " << endpoint << std::endl;
            } catch (const zmq::error_t& e) {
                std::cout << "[MetricsAggregator] Warning: Failed to connect to " 
                          << endpoint << ": " << e.what() << std::endl;
            }
        }
        
        // Initialize default metrics
        initialize_default_metrics();
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[MetricsAggregator] Failed to initialize: " << e.what() << std::endl;
        return false;
    }
}

void MetricsAggregator::start() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    subscribe_thread_ = std::make_unique<std::thread>(&MetricsAggregator::subscribe_loop, this);
    cleanup_thread_ = std::make_unique<std::thread>(&MetricsAggregator::cleanup_loop, this);
    
    std::cout << "[MetricsAggregator] Started metrics aggregation" << std::endl;
}

void MetricsAggregator::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    if (subscribe_thread_ && subscribe_thread_->joinable()) {
        subscribe_thread_->join();
    }
    
    if (cleanup_thread_ && cleanup_thread_->joinable()) {
        cleanup_thread_->join();
    }
    
    if (subscriber_) {
        subscriber_->close();
    }
    
    std::cout << "[MetricsAggregator] Stopped" << std::endl;
}

void MetricsAggregator::initialize_default_metrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Initialize all HFT metrics with default values and service labels
    auto init_metric = [this](const char* name, MetricType type, uint64_t default_value = 0) {
        MetricStats stats;
        stats.name = name;
        stats.type = type;
        stats.count = 0;
        stats.sum = default_value;
        stats.min_value = default_value;
        stats.max_value = default_value;
        stats.p50 = default_value;
        stats.p90 = default_value;
        stats.p95 = default_value;
        stats.p99 = default_value;
        stats.p999 = default_value;
        stats.mean = default_value;
        stats.service_name = "system"; // Default service for system-wide metrics
        default_metrics_[name] = stats;
    };
    
    // Critical Path Latencies with updated metric names (no prefixes)
    init_metric(hft::metrics::TOTAL_LATENCY, MetricType::LATENCY);
    init_metric(hft::metrics::PARSE_LATENCY, MetricType::LATENCY);
    init_metric(hft::metrics::PUBLISH_LATENCY, MetricType::LATENCY);
    init_metric(hft::metrics::PROCESS_LATENCY, MetricType::LATENCY);
    init_metric(hft::metrics::RISK_CHECK_LATENCY, MetricType::LATENCY);
    init_metric(hft::metrics::SUBMIT_LATENCY, MetricType::LATENCY);
    init_metric(hft::metrics::TICK_TO_SIGNAL, MetricType::LATENCY);
    init_metric(hft::metrics::TICK_TO_ORDER, MetricType::LATENCY);
    init_metric(hft::metrics::TICK_TO_FILL, MetricType::LATENCY);
    
    // Throughput Metrics with updated metric names (no prefixes)
    init_metric(hft::metrics::MESSAGES_PROCESSED, MetricType::COUNTER);
    init_metric(hft::metrics::MESSAGES_PER_SECOND, MetricType::GAUGE);
    init_metric(hft::metrics::SIGNALS_GENERATED, MetricType::COUNTER);
    init_metric(hft::metrics::DECISIONS_PER_SECOND, MetricType::GAUGE);
    init_metric(hft::metrics::ORDERS_SUBMITTED_TOTAL, MetricType::COUNTER);
    init_metric(hft::metrics::ORDERS_FILLED_TOTAL, MetricType::COUNTER);
    init_metric(hft::metrics::ORDERS_PER_SECOND, MetricType::GAUGE);
    
    // Trading Performance with updated metric names (no prefixes)
    init_metric(hft::metrics::POSITIONS_OPEN_COUNT, MetricType::GAUGE);
    init_metric(hft::metrics::PNL_TOTAL_USD, MetricType::GAUGE);
    init_metric(hft::metrics::FILL_RATE_PERCENT, MetricType::GAUGE, 100); // Default 100%
    
    // System Metrics with updated metric names (no prefixes)
    init_metric(hft::metrics::MEMORY_RSS_MB, MetricType::GAUGE);
    init_metric(hft::metrics::CPU_USAGE_PERCENT, MetricType::GAUGE);
    init_metric(hft::metrics::THREAD_COUNT, MetricType::GAUGE, 1);
    
    std::cout << "[MetricsAggregator] Initialized " << default_metrics_.size() << " default metrics" << std::endl;
}

void MetricsAggregator::subscribe_loop() {
    while (running_.load()) {
        try {
            zmq::message_t message;
            auto result = subscriber_->recv(message, zmq::recv_flags::dontwait);
            
            if (result && message.size() >= sizeof(MetricsMessage)) {
                std::vector<uint8_t> data(static_cast<uint8_t*>(message.data()),
                                         static_cast<uint8_t*>(message.data()) + message.size());
                process_metrics_message(data);
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN && e.num() != ETERM) {
                std::cerr << "[MetricsAggregator] Receive error: " << e.what() << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void MetricsAggregator::cleanup_loop() {
    while (running_.load()) {
        uint64_t now_ns = HighResTimer::get_nanoseconds();
        
        // Limit scope of mutex lock - don't hold it during sleep!
        {
            std::lock_guard<std::mutex> lock(metrics_mutex_);
            for (auto& [service_name, service_metrics] : service_metrics_) {
                if (service_metrics.is_online && 
                    (now_ns - service_metrics.last_update_ns) > SERVICE_TIMEOUT_NS) {
                    mark_service_offline(service_name);
                }
            }
        }
        
        // Sleep OUTSIDE the mutex lock
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void MetricsAggregator::process_metrics_message(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(MetricsMessage)) {
        return;
    }
    
    const MetricsMessage* header = reinterpret_cast<const MetricsMessage*>(data.data());
    std::string service_name(header->service_name);
    
    // Validate message size
    size_t expected_size = sizeof(MetricsMessage) + (header->metric_count * sizeof(SerializedMetricEntry));
    if (data.size() < expected_size) {
        std::cerr << "[MetricsAggregator] Invalid message size from " << service_name << std::endl;
        return;
    }
    
    const SerializedMetricEntry* entries = reinterpret_cast<const SerializedMetricEntry*>(data.data() + sizeof(MetricsMessage));
    
    std::cout << "[MetricsAggregator] process_metrics_message() - trying to acquire mutex for service: " << service_name << std::endl;
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::cout << "[MetricsAggregator] process_metrics_message() - mutex acquired, processing " << header->metric_count << " metrics from " << service_name << std::endl;
    ServiceMetrics& service_metrics = service_metrics_[service_name];
    service_metrics.service_name = service_name;
    service_metrics.last_update_ns = header->timestamp_ns;
    service_metrics.is_online = true;
    service_metrics.metrics.clear();
    
    // Process each metric entry with service identification
    for (uint32_t i = 0; i < header->metric_count; ++i) {
        const SerializedMetricEntry& entry = entries[i];
        std::string metric_name(entry.name);
        
        MetricStats stats;
        stats.name = metric_name;
        stats.service_name = service_name;  // Set the service that produced this metric
        stats.type = static_cast<MetricType>(entry.type);
        stats.count = 1;
        stats.sum = entry.value;
        stats.min_value = entry.value;
        stats.max_value = entry.value;
        stats.p50 = entry.value;
        stats.p90 = entry.value;
        stats.p95 = entry.value;
        stats.p99 = entry.value;
        stats.p999 = entry.value;
        stats.mean = entry.value;
        
        // Use service-prefixed key for unique identification
        std::string service_metric_key = service_name + "." + metric_name;
        service_metrics.metrics[service_metric_key] = stats;
    }
    std::cout << "[MetricsAggregator] process_metrics_message() - finished processing metrics from " << service_name << ", releasing mutex..." << std::endl;
}

void MetricsAggregator::mark_service_offline(const std::string& service_name) {
    auto it = service_metrics_.find(service_name);
    if (it != service_metrics_.end()) {
        it->second.is_online = false;
        std::cout << "[MetricsAggregator] Service " << service_name << " marked offline" << std::endl;
    }
}

std::unordered_map<std::string, MetricStats> MetricsAggregator::get_all_metrics() const {
    std::cout << "[MetricsAggregator] get_all_metrics() - trying to acquire mutex..." << std::endl;
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::cout << "[MetricsAggregator] get_all_metrics() - mutex acquired, processing metrics..." << std::endl;
    
    // Start with default metrics
    auto result = default_metrics_;
    
    // Override with actual metrics from online services
    // Keep service-specific keys to avoid conflicts between services
    for (const auto& [service_name, service_metrics] : service_metrics_) {
        if (service_metrics.is_online) {
            for (const auto& [service_metric_key, stats] : service_metrics.metrics) {
                result[service_metric_key] = stats;
            }
        }
    }
    
    std::cout << "[MetricsAggregator] get_all_metrics() - returning " << result.size() << " metrics, releasing mutex..." << std::endl;
    return result;
}

std::vector<std::string> MetricsAggregator::get_online_services() const {
    std::cout << "[MetricsAggregator] get_online_services() - trying to acquire mutex..." << std::endl;
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::cout << "[MetricsAggregator] get_online_services() - mutex acquired" << std::endl;
    
    std::vector<std::string> online_services;
    for (const auto& [service_name, service_metrics] : service_metrics_) {
        if (service_metrics.is_online) {
            online_services.push_back(service_name);
        }
    }
    
    std::cout << "[MetricsAggregator] get_online_services() - returning " << online_services.size() << " services, releasing mutex..." << std::endl;
    return online_services;
}

std::unordered_map<std::string, MetricStats> MetricsAggregator::get_service_metrics(const std::string& service_name) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::unordered_map<std::string, MetricStats> result;
    
    // Get default metrics for this service
    for (const auto& [metric_name, stats] : default_metrics_) {
        MetricStats service_stats = stats;
        service_stats.service_name = service_name;
        result[metric_name] = service_stats;
    }
    
    // Override with actual metrics if service is online
    auto service_it = service_metrics_.find(service_name);
    if (service_it != service_metrics_.end() && service_it->second.is_online) {
        for (const auto& [service_metric_key, stats] : service_it->second.metrics) {
            // Extract metric name from service_metric_key (remove service prefix)
            std::string metric_name = service_metric_key;
            size_t dot_pos = metric_name.find('.');
            if (dot_pos != std::string::npos) {
                metric_name = metric_name.substr(dot_pos + 1);
            }
            result[metric_name] = stats;
        }
    }
    
    return result;
}

} // namespace hft