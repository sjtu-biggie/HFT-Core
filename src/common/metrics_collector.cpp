#include "metrics_collector.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

namespace hft {

// Thread-local storage definitions
thread_local std::unique_ptr<MetricsRingBuffer<>> MetricsCollector::thread_buffer_;
thread_local std::unordered_map<std::string, HighResTimer::ticks_t> MetricsCollector::thread_timers_;

MetricsCollector& MetricsCollector::instance() {
    static MetricsCollector instance;
    return instance;
}

void MetricsCollector::initialize() {
    if (initialized_.load()) {
        return;
    }
    
    std::cout << "[MetricsCollector] Initializing metrics collection system..." << std::endl;
    
    // Initialize high-resolution timer first
    HighResTimer::initialize();
    
    // Start collection thread
    shutdown_requested_.store(false);
    collection_thread_ = std::make_unique<std::thread>(&MetricsCollector::collection_thread_main, this);
    
    initialized_.store(true);
    std::cout << "[MetricsCollector] Metrics collection system initialized" << std::endl;
}

void MetricsCollector::shutdown() {
    if (!initialized_.load()) {
        return;
    }
    
    std::cout << "[MetricsCollector] Shutting down metrics collection system..." << std::endl;
    
    // Stop monitoring if running
    stop_monitoring_thread();
    
    // Stop collection thread
    shutdown_requested_.store(true);
    if (collection_thread_ && collection_thread_->joinable()) {
        collection_thread_->join();
    }
    
    // Final collection
    collect_from_all_threads();
    
    initialized_.store(false);
    std::cout << "[MetricsCollector] Metrics collection system shutdown complete" << std::endl;
}

void MetricsCollector::record_latency(const char* label, uint64_t nanoseconds) {
    auto* buffer = get_thread_buffer();
    if (buffer) {
        buffer->push(MetricEntry(label, nanoseconds, MetricType::LATENCY));
    }
}

void MetricsCollector::increment_counter(const char* label) {
    auto* buffer = get_thread_buffer();
    if (buffer) {
        buffer->push(MetricEntry(label, 1, MetricType::COUNTER));
    }
}

void MetricsCollector::set_gauge(const char* label, uint64_t value) {
    auto* buffer = get_thread_buffer();
    if (buffer) {
        buffer->push(MetricEntry(label, value, MetricType::GAUGE));
    }
}

void MetricsCollector::record_histogram_value(const char* label, uint64_t value) {
    auto* buffer = get_thread_buffer();
    if (buffer) {
        buffer->push(MetricEntry(label, value, MetricType::HISTOGRAM));
    }
}

void MetricsCollector::start_timer(const char* label) {
    thread_timers_[std::string(label)] = HighResTimer::get_ticks();
}

void MetricsCollector::end_timer(const char* label) {
    auto it = thread_timers_.find(std::string(label));
    if (it != thread_timers_.end()) {
        uint64_t elapsed_ns = HighResTimer::ticks_to_nanoseconds(
            HighResTimer::get_ticks() - it->second);
        record_latency(label, elapsed_ns);
        thread_timers_.erase(it);
    }
}

MetricsRingBuffer<>* MetricsCollector::get_thread_buffer() {
    if (!thread_buffer_) {
        thread_buffer_ = std::make_unique<MetricsRingBuffer<>>();
        
        // Register this buffer for collection
        std::lock_guard<std::mutex> lock(buffers_mutex_);
        thread_buffers_.push_back(thread_buffer_.get());
    }
    return thread_buffer_.get();
}

std::unordered_map<std::string, MetricStats> MetricsCollector::get_statistics() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(stats_mutex_));
    return statistics_;
}

void MetricsCollector::collect_from_all_threads() {
    std::lock_guard<std::mutex> buffers_lock(buffers_mutex_);
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    
    // Collect from all thread buffers
    for (auto* buffer : thread_buffers_) {
        if (!buffer) continue;
        
        MetricEntry entry;
        while (buffer->pop(entry)) {
            std::string label(entry.label);
            
            // Get or create metric stats
            auto& stats = statistics_[label];
            if (stats.name.empty()) {
                stats.name = label;
                stats.type = entry.type;
            }
            
            // Update statistics based on type
            switch (entry.type) {
                case MetricType::LATENCY:
                case MetricType::HISTOGRAM:
                    stats.update(entry.value);
                    break;
                    
                case MetricType::COUNTER:
                    stats.count += entry.value;
                    stats.sum += entry.value;
                    break;
                    
                case MetricType::GAUGE:
                    stats.recent_values.push_back(entry.value);
                    if (stats.recent_values.size() > 100) {
                        stats.recent_values.erase(stats.recent_values.begin());
                    }
                    if (!stats.recent_values.empty()) {
                        stats.sum = stats.recent_values.back(); // Latest value for gauges
                    }
                    break;
            }
        }
    }
}

void MetricsCollector::collection_thread_main() {
    std::cout << "[MetricsCollector] Collection thread started" << std::endl;
    
    while (!shutdown_requested_.load()) {
        collect_from_all_threads();
        
        // Sleep for 100ms between collections
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[MetricsCollector] Collection thread stopped" << std::endl;
}

void MetricsCollector::start_monitoring_thread(int interval_ms) {
    if (monitoring_active_.load()) {
        return;
    }
    
    monitoring_active_.store(true);
    monitoring_thread_ = std::make_unique<std::thread>(
        &MetricsCollector::monitoring_thread_main, this, interval_ms);
    
    std::cout << "[MetricsCollector] Monitoring thread started (interval: " 
              << interval_ms << "ms)" << std::endl;
}

void MetricsCollector::stop_monitoring_thread() {
    if (!monitoring_active_.load()) {
        return;
    }
    
    monitoring_active_.store(false);
    monitoring_cv_.notify_all();
    
    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
    }
    
    std::cout << "[MetricsCollector] Monitoring thread stopped" << std::endl;
}

void MetricsCollector::monitoring_thread_main(int interval_ms) {
    auto interval = std::chrono::milliseconds(interval_ms);
    
    while (monitoring_active_.load()) {
        std::unique_lock<std::mutex> lock(monitoring_mutex_);
        
        // Wait for interval or shutdown
        if (monitoring_cv_.wait_for(lock, interval, [this] { 
            return !monitoring_active_.load(); 
        })) {
            break; // Shutdown requested
        }
        
        // Print current statistics
        auto stats = get_statistics();
        if (!stats.empty()) {
            std::cout << "\n=== HFT Metrics Report ===" << std::endl;
            
            for (const auto& [name, metric] : stats) {
                if (metric.type == MetricType::LATENCY && metric.count > 0) {
                    std::cout << std::setw(30) << name << ": "
                              << std::setw(8) << metric.p50 << "ns (p50) "
                              << std::setw(8) << metric.p99 << "ns (p99) "
                              << std::setw(8) << metric.max_value << "ns (max) "
                              << "count=" << metric.count << std::endl;
                } else if (metric.type == MetricType::COUNTER) {
                    std::cout << std::setw(30) << name << ": "
                              << metric.count << " total" << std::endl;
                } else if (metric.type == MetricType::GAUGE && !metric.recent_values.empty()) {
                    std::cout << std::setw(30) << name << ": "
                              << metric.recent_values.back() << " (current)" << std::endl;
                }
            }
            std::cout << "=========================" << std::endl;
        }
    }
}

void MetricsCollector::clear() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    statistics_.clear();
}

std::string MetricsCollector::export_to_csv() const {
    auto stats = get_statistics();
    std::ostringstream oss;
    
    // CSV header
    oss << "metric_name,type,count,min_ns,max_ns,mean_ns,p50_ns,p90_ns,p95_ns,p99_ns,p999_ns\n";
    
    for (const auto& [name, metric] : stats) {
        oss << name << ","
            << static_cast<int>(metric.type) << ","
            << metric.count << ","
            << metric.min_value << ","
            << metric.max_value << ","
            << static_cast<uint64_t>(metric.mean) << ","
            << metric.p50 << ","
            << metric.p90 << ","
            << metric.p95 << ","
            << metric.p99 << ","
            << metric.p999 << "\n";
    }
    
    return oss.str();
}

std::string MetricsCollector::export_to_json() const {
    auto stats = get_statistics();
    std::ostringstream oss;
    
    oss << "{\n  \"metrics\": [\n";
    
    bool first = true;
    for (const auto& [name, metric] : stats) {
        if (!first) oss << ",\n";
        first = false;
        
        oss << "    {\n"
            << "      \"name\": \"" << name << "\",\n"
            << "      \"type\": " << static_cast<int>(metric.type) << ",\n"
            << "      \"count\": " << metric.count << ",\n"
            << "      \"min_ns\": " << metric.min_value << ",\n"
            << "      \"max_ns\": " << metric.max_value << ",\n"
            << "      \"mean_ns\": " << static_cast<uint64_t>(metric.mean) << ",\n"
            << "      \"p50_ns\": " << metric.p50 << ",\n"
            << "      \"p90_ns\": " << metric.p90 << ",\n"
            << "      \"p95_ns\": " << metric.p95 << ",\n"
            << "      \"p99_ns\": " << metric.p99 << ",\n"
            << "      \"p999_ns\": " << metric.p999 << "\n"
            << "    }";
    }
    
    oss << "\n  ],\n";
    oss << "  \"timestamp\": " << HighResTimer::get_nanoseconds() << ",\n";
    oss << "  \"timer_info\": \"" << HighResTimer::get_timer_info() << "\"\n";
    oss << "}\n";
    
    return oss.str();
}

void MetricsCollector::export_to_file(const std::string& filename, const std::string& format) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[MetricsCollector] Error: Could not open file for export: " << filename << std::endl;
        return;
    }
    
    if (format == "json") {
        file << export_to_json();
    } else {
        file << export_to_csv();
    }
    
    file.close();
    std::cout << "[MetricsCollector] Metrics exported to: " << filename << std::endl;
}

void MetricStats::calculate_percentiles() {
    if (recent_values.empty()) return;
    
    auto sorted_values = recent_values;
    std::sort(sorted_values.begin(), sorted_values.end());
    
    size_t size = sorted_values.size();
    p50 = sorted_values[size * 50 / 100];
    p90 = sorted_values[size * 90 / 100];
    p95 = sorted_values[size * 95 / 100];
    p99 = sorted_values[size * 99 / 100];
    p999 = sorted_values[size * 999 / 1000];
}

} // namespace hft