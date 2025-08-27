#pragma once

#include "high_res_timer.h"
#include <atomic>
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace hft {

// Metric types for different kinds of measurements
enum class MetricType : uint8_t {
    LATENCY = 0,      // Timing measurements in nanoseconds
    COUNTER = 1,      // Monotonic counters
    GAUGE = 2,        // Current value measurements
    HISTOGRAM = 3     // Distribution of values
};

// Individual metric entry - designed for lock-free operation
struct MetricEntry {
    HighResTimer::ticks_t timestamp;
    uint64_t value;
    const char* label;
    MetricType type;
    uint32_t thread_id;
    
    MetricEntry() : timestamp(0), value(0), label(""), type(MetricType::COUNTER), thread_id(0) {}
    
    MetricEntry(const char* lbl, uint64_t val, MetricType t)
        : timestamp(HighResTimer::get_ticks()), value(val), label(lbl), type(t), thread_id(0) {}
};

// Lock-free circular buffer for metrics (one per thread)
template<size_t CAPACITY = 1048576>  // 1M entries per thread
class MetricsRingBuffer {
public:
    MetricsRingBuffer() : head_(0), tail_(0) {}
    
    // Add metric entry (lock-free for single producer)
    bool push(const MetricEntry& entry) {
        uint64_t current_head = head_.load(std::memory_order_relaxed);
        uint64_t next_head = (current_head + 1) % CAPACITY;
        
        if (next_head == tail_.load(std::memory_order_acquire)) {
            // Buffer full - could increment a drop counter here
            return false;
        }
        
        buffer_[current_head] = entry;
        head_.store(next_head, std::memory_order_release);
        return true;
    }
    
    // Pop metric entry (single consumer)
    bool pop(MetricEntry& entry) {
        uint64_t current_tail = tail_.load(std::memory_order_relaxed);
        
        if (current_tail == head_.load(std::memory_order_acquire)) {
            // Buffer empty
            return false;
        }
        
        entry = buffer_[current_tail];
        tail_.store((current_tail + 1) % CAPACITY, std::memory_order_release);
        return true;
    }
    
    // Get approximate size (for monitoring)
    size_t size() const {
        uint64_t h = head_.load(std::memory_order_relaxed);
        uint64_t t = tail_.load(std::memory_order_relaxed);
        return (h >= t) ? (h - t) : (CAPACITY - t + h);
    }
    
    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }
    
    bool full() const {
        uint64_t next_head = (head_.load(std::memory_order_relaxed) + 1) % CAPACITY;
        return next_head == tail_.load(std::memory_order_relaxed);
    }

private:
    std::array<MetricEntry, CAPACITY> buffer_;
    alignas(64) std::atomic<uint64_t> head_;  // Cache line aligned
    alignas(64) std::atomic<uint64_t> tail_;  // Cache line aligned
};

// Statistics for a metric
struct MetricStats {
    std::string name;
    MetricType type;
    uint64_t count = 0;
    uint64_t min_value = UINT64_MAX;
    uint64_t max_value = 0;
    uint64_t sum = 0;
    double mean = 0.0;
    uint64_t p50 = 0;
    uint64_t p90 = 0;
    uint64_t p95 = 0;
    uint64_t p99 = 0;
    uint64_t p999 = 0;
    
    // Recent values for trend analysis
    std::vector<uint64_t> recent_values;
    
    void update(uint64_t value) {
        count++;
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
        sum += value;
        mean = static_cast<double>(sum) / count;
        
        // Keep last 1000 values for percentile calculation
        recent_values.push_back(value);
        if (recent_values.size() > 1000) {
            recent_values.erase(recent_values.begin());
        }
        
        // Calculate percentiles if we have enough data
        if (recent_values.size() >= 10) {
            calculate_percentiles();
        }
    }
    
private:
    void calculate_percentiles();
};

// Main metrics collection system
class MetricsCollector {
public:
    static MetricsCollector& instance();
    
    // Initialize metrics system
    void initialize();
    
    // Shutdown metrics system
    void shutdown();
    
    // Record different types of metrics
    void record_latency(const char* label, uint64_t nanoseconds);
    void increment_counter(const char* label);
    void set_gauge(const char* label, uint64_t value);
    void record_histogram_value(const char* label, uint64_t value);
    
    // Timing helpers
    void start_timer(const char* label);
    void end_timer(const char* label);
    
    // Get thread-local metrics buffer
    MetricsRingBuffer<>* get_thread_buffer();
    
    // Get collected statistics
    std::unordered_map<std::string, MetricStats> get_statistics() const;
    
    // Export metrics to different formats
    std::string export_to_csv() const;
    std::string export_to_json() const;
    void export_to_file(const std::string& filename, const std::string& format = "csv") const;
    
    // Real-time monitoring
    void start_monitoring_thread(int interval_ms = 100);
    void stop_monitoring_thread();
    
    // Clear all metrics
    void clear();

private:
    MetricsCollector() = default;
    ~MetricsCollector() = default;
    
    // Thread-local storage for metrics buffers
    thread_local static std::unique_ptr<MetricsRingBuffer<>> thread_buffer_;
    thread_local static std::unordered_map<std::string, HighResTimer::ticks_t> thread_timers_;
    
    // Global state
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_requested_{false};
    
    // Collection thread
    std::unique_ptr<std::thread> collection_thread_;
    std::mutex stats_mutex_;
    std::unordered_map<std::string, MetricStats> statistics_;
    
    // Monitoring thread
    std::unique_ptr<std::thread> monitoring_thread_;
    std::atomic<bool> monitoring_active_{false};
    std::condition_variable monitoring_cv_;
    std::mutex monitoring_mutex_;
    
    // Collection methods
    void collect_from_all_threads();
    void collection_thread_main();
    void monitoring_thread_main(int interval_ms);
    
    // Registered thread buffers (for collection)
    std::mutex buffers_mutex_;
    std::vector<MetricsRingBuffer<>*> thread_buffers_;
};

// RAII metrics timer
class MetricsTimer {
public:
    explicit MetricsTimer(const char* label)
        : label_(label), start_ticks_(HighResTimer::get_ticks()) {}
    
    ~MetricsTimer() {
        uint64_t elapsed_ns = HighResTimer::ticks_to_nanoseconds(
            HighResTimer::get_ticks() - start_ticks_);
        MetricsCollector::instance().record_latency(label_, elapsed_ns);
    }

private:
    const char* label_;
    HighResTimer::ticks_t start_ticks_;
};

// Convenient macros for metrics collection
#define HFT_METRICS_TIMER(label) hft::MetricsTimer _metrics_timer(label)
#define HFT_METRICS_LATENCY(label, ns) hft::MetricsCollector::instance().record_latency(label, ns)
#define HFT_METRICS_COUNTER(label) hft::MetricsCollector::instance().increment_counter(label)
#define HFT_METRICS_GAUGE(label, value) hft::MetricsCollector::instance().set_gauge(label, value)
#define HFT_METRICS_HISTOGRAM(label, value) hft::MetricsCollector::instance().record_histogram_value(label, value)

// Strategic timing points for HFT critical paths
namespace metrics {
    // Market data path
    constexpr const char* MARKET_DATA_RECEIVE = "market_data.receive_latency";
    constexpr const char* MARKET_DATA_PARSE = "market_data.parse_latency";
    constexpr const char* MARKET_DATA_PUBLISH = "market_data.publish_latency";
    
    // Strategy path
    constexpr const char* STRATEGY_PROCESS = "strategy.process_latency";
    constexpr const char* SIGNAL_GENERATION = "strategy.signal_generation_latency";
    constexpr const char* SIGNAL_PUBLISH = "strategy.signal_publish_latency";
    
    // Order path
    constexpr const char* ORDER_RECEIVE = "order.receive_latency";
    constexpr const char* ORDER_PROCESS = "order.process_latency";
    constexpr const char* ORDER_SEND = "order.send_latency";
    
    // End-to-end timing
    constexpr const char* TICK_TO_SIGNAL = "e2e.tick_to_signal_latency";
    constexpr const char* SIGNAL_TO_ORDER = "e2e.signal_to_order_latency";
    constexpr const char* TICK_TO_ORDER = "e2e.tick_to_order_latency";
    
    // Throughput counters
    constexpr const char* MARKET_DATA_MESSAGES = "throughput.market_data_messages";
    constexpr const char* SIGNALS_GENERATED = "throughput.signals_generated";
    constexpr const char* ORDERS_PROCESSED = "throughput.orders_processed";
    
    // System health gauges
    constexpr const char* MEMORY_USAGE = "system.memory_usage_mb";
    constexpr const char* CPU_USAGE = "system.cpu_usage_percent";
    constexpr const char* QUEUE_DEPTH = "system.queue_depth";
}

} // namespace hft