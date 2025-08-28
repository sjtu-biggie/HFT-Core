#pragma once

#include "metrics_collector.h"
#include "hft_metrics.h"
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <chrono>
#include <iomanip>
#include <set>

namespace hft {

// Prometheus metrics exporter for HFT system
class PrometheusExporter {
public:
    static std::string export_metrics(const std::unordered_map<std::string, MetricStats>* external_metrics = nullptr) {
        std::ostringstream prometheus_output;
        
        // Add system information
        add_system_info(prometheus_output);
        
        // Use external metrics if provided, otherwise fall back to local collector
        std::unordered_map<std::string, MetricStats> stats;
        if (external_metrics) {
            stats = *external_metrics;
        } else {
            auto& collector = MetricsCollector::instance();
            stats = collector.get_statistics();
        }
        
        // Metrics that are handled by HFT-specific export functions to avoid duplicates
        std::set<std::string> hft_handled_metrics = {
            "e2e.tick_to_signal_ns", "e2e.tick_to_order_ns", "e2e.tick_to_fill_ns",
            "md.total_latency_ns", "strategy.total_latency_ns", "order.total_latency_ns",
            "md.messages_per_second", "strategy.decisions_per_second", "orders.per_second",
            "trading.positions_open", "trading.pnl_total_usd", "trading.fill_rate_percent",
            "system.memory_rss_mb", "system.cpu_usage_percent", "system.thread_count",
            "network.bytes_received_total"
        };
        
        for (const auto& [name, metric_stats] : stats) {
            if (hft_handled_metrics.find(name) == hft_handled_metrics.end()) {
                export_metric(prometheus_output, name, metric_stats);
            }
        }
        
        // Add custom HFT metrics
        add_hft_specific_metrics(prometheus_output, stats);
        
        return prometheus_output.str();
    }
    
    static std::string get_content_type() {
        return "text/plain; version=0.0.4; charset=utf-8";
    }

private:
    static void add_system_info(std::ostringstream& output) {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        // System uptime and build info
        output << "# HELP hft_system_info HFT system build and version information\n";
        output << "# TYPE hft_system_info gauge\n";
        output << "hft_system_info{version=\"2.0\",build=\"" << __DATE__ << " " << __TIME__ << "\"} 1\n";
        
        // Current timestamp
        output << "# HELP hft_scrape_timestamp_ms Timestamp of metrics scrape\n";
        output << "# TYPE hft_scrape_timestamp_ms gauge\n";
        output << "hft_scrape_timestamp_ms " << timestamp << "\n";
        
        // Process information
        output << "# HELP hft_process_start_time_seconds Process start time in unix timestamp\n";
        output << "# TYPE hft_process_start_time_seconds gauge\n";
        output << "hft_process_start_time_seconds " << (timestamp / 1000.0) << "\n";
    }
    
    static void export_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        std::string metric_name = sanitize_metric_name(name);
        
        switch (stats.type) {
            case MetricType::LATENCY:
                export_latency_metric(output, metric_name, stats);
                break;
            case MetricType::COUNTER:
                export_counter_metric(output, metric_name, stats);
                break;
            case MetricType::GAUGE:
                export_gauge_metric(output, metric_name, stats);
                break;
            case MetricType::HISTOGRAM:
                export_histogram_metric(output, metric_name, stats);
                break;
        }
    }
    
    static void export_latency_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        // Export as histogram with predefined buckets for latency
        output << "# HELP hft_" << name << "_nanoseconds Latency measurements in nanoseconds\n";
        output << "# TYPE hft_" << name << "_nanoseconds histogram\n";
        
        // Define latency buckets (in nanoseconds)
        std::vector<uint64_t> buckets = {100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000};
        
        uint64_t cumulative_count = 0;
        for (uint64_t bucket : buckets) {
            // Estimate count in this bucket (simplified)
            uint64_t bucket_count = (stats.min_value <= bucket) ? stats.count : 0;
            cumulative_count += bucket_count;
            
            output << "hft_" << name << "_nanoseconds_bucket{le=\"" << bucket << "\"} " << cumulative_count << "\n";
        }
        
        output << "hft_" << name << "_nanoseconds_bucket{le=\"+Inf\"} " << stats.count << "\n";
        output << "hft_" << name << "_nanoseconds_count " << stats.count << "\n";
        output << "hft_" << name << "_nanoseconds_sum " << (stats.sum) << "\n";
        
        // Also export percentiles as separate gauges
        output << "# HELP hft_" << name << "_p50_nanoseconds 50th percentile latency\n";
        output << "# TYPE hft_" << name << "_p50_nanoseconds gauge\n";
        output << "hft_" << name << "_p50_nanoseconds " << stats.p50 << "\n";
        
        output << "# HELP hft_" << name << "_p95_nanoseconds 95th percentile latency\n";
        output << "# TYPE hft_" << name << "_p95_nanoseconds gauge\n";
        output << "hft_" << name << "_p95_nanoseconds " << stats.p95 << "\n";
        
        output << "# HELP hft_" << name << "_p99_nanoseconds 99th percentile latency\n";
        output << "# TYPE hft_" << name << "_p99_nanoseconds gauge\n";
        output << "hft_" << name << "_p99_nanoseconds " << stats.p99 << "\n";
    }
    
    static void export_counter_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        output << "# HELP hft_" << name << "_total Total count of " << name << "\n";
        output << "# TYPE hft_" << name << "_total counter\n";
        output << "hft_" << name << "_total " << stats.sum << "\n";
    }
    
    static void export_gauge_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        output << "# HELP hft_" << name << " Current value of " << name << "\n";
        output << "# TYPE hft_" << name << " gauge\n";
        output << "hft_" << name << " " << stats.max_value << "\n";  // Use most recent max as current
        
        // Also export min/max if available
        if (stats.count > 0) {
            output << "# HELP hft_" << name << "_min Minimum observed value\n";
            output << "# TYPE hft_" << name << "_min gauge\n";
            output << "hft_" << name << "_min " << stats.min_value << "\n";
            
            output << "# HELP hft_" << name << "_max Maximum observed value\n";
            output << "# TYPE hft_" << name << "_max gauge\n";
            output << "hft_" << name << "_max " << stats.max_value << "\n";
        }
    }
    
    static void export_histogram_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        output << "# HELP hft_" << name << " Distribution of " << name << "\n";
        output << "# TYPE hft_" << name << " histogram\n";
        
        // Simple histogram export (would need more sophisticated bucketing in production)
        output << "hft_" << name << "_bucket{le=\"+Inf\"} " << stats.count << "\n";
        output << "hft_" << name << "_count " << stats.count << "\n";
        output << "hft_" << name << "_sum " << stats.sum << "\n";
    }
    
    static void add_hft_specific_metrics(std::ostringstream& output, const std::unordered_map<std::string, MetricStats>& stats) {
        
        // Export critical latency metrics with proper buckets
        add_hft_latency_metrics(output, stats);
        
        // Export throughput metrics  
        add_hft_throughput_metrics(output, stats);
        
        // Export trading performance metrics
        add_hft_trading_metrics(output, stats);
        
        // Export system health metrics
        add_hft_system_metrics(output, stats);
        
        // Export component status
        add_hft_component_status(output);
    }
    
    static void add_hft_latency_metrics(std::ostringstream& output, const auto& stats) {
        // Critical path latencies with appropriate buckets for HFT
        std::vector<std::string> critical_latencies = {
            "e2e.tick_to_signal_ns", "e2e.tick_to_order_ns", "e2e.tick_to_fill_ns",
            "md.total_latency_ns", "strategy.total_latency_ns", "order.total_latency_ns"
        };
        
        for (const auto& metric_name : critical_latencies) {
            auto it = stats.find(metric_name);
            if (it != stats.end() && it->second.type == MetricType::LATENCY) {
                export_hft_latency_histogram(output, metric_name, it->second);
            }
        }
    }
    
    static void add_hft_throughput_metrics(std::ostringstream& output, const auto& stats) {
        // Message throughput metrics
        std::vector<std::pair<std::string, std::string>> throughput_metrics = {
            {"md.messages_per_second", "Market data messages per second"},
            {"strategy.decisions_per_second", "Strategy decisions per second"}, 
            {"orders.per_second", "Orders per second"}
        };
        
        for (const auto& [metric_name, help_text] : throughput_metrics) {
            auto it = stats.find(metric_name);
            if (it != stats.end()) {
                output << "# HELP hft_" << sanitize_metric_name(metric_name) << " " << help_text << "\n";
                output << "# TYPE hft_" << sanitize_metric_name(metric_name) << " gauge\n";
                output << "hft_" << sanitize_metric_name(metric_name) << " " << it->second.max_value << "\n";
            }
        }
    }
    
    static void add_hft_trading_metrics(std::ostringstream& output, const auto& stats) {
        // Trading performance
        output << "# HELP hft_trading_positions_open Current open positions\n";
        output << "# TYPE hft_trading_positions_open gauge\n";
        auto it = stats.find("trading.positions_open");
        output << "hft_trading_positions_open " << (it != stats.end() ? it->second.max_value : 0) << "\n";
        
        output << "# HELP hft_trading_pnl_total_usd Total P&L in USD\n";
        output << "# TYPE hft_trading_pnl_total_usd gauge\n";
        it = stats.find("trading.pnl_total_usd");
        output << "hft_trading_pnl_total_usd " << (it != stats.end() ? it->second.sum : 0) << "\n";
        
        output << "# HELP hft_trading_fill_rate_percent Order fill rate percentage\n";
        output << "# TYPE hft_trading_fill_rate_percent gauge\n";
        it = stats.find("trading.fill_rate_percent");
        output << "hft_trading_fill_rate_percent " << (it != stats.end() ? it->second.max_value : 100) << "\n";
    }
    
    static void add_hft_system_metrics(std::ostringstream& output, const auto& stats) {
        // System resource utilization  
        output << "# HELP hft_system_memory_rss_mb RSS memory usage in MB\n";
        output << "# TYPE hft_system_memory_rss_mb gauge\n";
        auto it = stats.find("system.memory_rss_mb");
        output << "hft_system_memory_rss_mb " << (it != stats.end() ? it->second.max_value : 0) << "\n";
        
        output << "# HELP hft_system_cpu_usage_percent CPU usage percentage\n";
        output << "# TYPE hft_system_cpu_usage_percent gauge\n";
        it = stats.find("system.cpu_usage_percent");
        output << "hft_system_cpu_usage_percent " << (it != stats.end() ? it->second.max_value : 0) << "\n";
        
        output << "# HELP hft_system_thread_count Active thread count\n";
        output << "# TYPE hft_system_thread_count gauge\n";
        it = stats.find("system.thread_count");
        output << "hft_system_thread_count " << (it != stats.end() ? it->second.max_value : 1) << "\n";
        
        // Network metrics
        output << "# HELP hft_network_bytes_received_total Network bytes received\n";
        output << "# TYPE hft_network_bytes_received_total counter\n";
        it = stats.find("network.bytes_received_total");
        output << "hft_network_bytes_received_total " << (it != stats.end() ? it->second.sum : 0) << "\n";
    }
    
    static void add_hft_component_status(std::ostringstream& output) {
        // Component health status - would be updated by actual components
        output << "# HELP hft_component_status Component operational status (1=healthy, 0=degraded)\n";
        output << "# TYPE hft_component_status gauge\n";
        output << "hft_component_status{component=\"market_data_handler\"} 1\n";
        output << "hft_component_status{component=\"strategy_engine\"} 1\n";
        output << "hft_component_status{component=\"order_gateway\"} 1\n";  
        output << "hft_component_status{component=\"position_risk_service\"} 1\n";
        output << "hft_component_status{component=\"logger\"} 1\n";
        
        // Service uptime
        output << "# HELP hft_service_uptime_seconds Service uptime in seconds\n";
        output << "# TYPE hft_service_uptime_seconds gauge\n";
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        output << "hft_service_uptime_seconds " << uptime << "\n";
    }
    
    static void export_hft_latency_histogram(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        std::string sanitized_name = sanitize_metric_name(name);
        
        // HFT-specific latency buckets (nanoseconds) - much tighter for trading systems
        std::vector<uint64_t> hft_buckets = {
            100, 250, 500, 1000,     // Sub-microsecond (100ns - 1μs)
            2500, 5000, 10000,       // Low microsecond (2.5μs - 10μs) 
            25000, 50000, 100000,    // High microsecond (25μs - 100μs)
            250000, 500000,          // Sub-millisecond (250μs - 500μs)
            1000000, 10000000        // Millisecond+ (1ms - 10ms)
        };
        
        output << "# HELP hft_" << sanitized_name << "_histogram HFT latency distribution\n";
        output << "# TYPE hft_" << sanitized_name << "_histogram histogram\n";
        
        // Estimate bucket counts based on percentiles
        uint64_t cumulative = 0;
        for (uint64_t bucket : hft_buckets) {
            uint64_t bucket_count = 0;
            if (bucket >= stats.p99) bucket_count = stats.count;
            else if (bucket >= stats.p95) bucket_count = stats.count * 99 / 100;
            else if (bucket >= stats.p90) bucket_count = stats.count * 95 / 100;
            else if (bucket >= stats.p50) bucket_count = stats.count * 90 / 100;
            else if (bucket >= stats.min_value) bucket_count = stats.count * 50 / 100;
            
            output << "hft_" << sanitized_name << "_histogram_bucket{le=\""
                   << bucket << "\"} " << bucket_count << "\n";
        }
        
        output << "hft_" << sanitized_name << "_histogram_bucket{le=\"+Inf\"} " << stats.count << "\n";
        output << "hft_" << sanitized_name << "_histogram_count " << stats.count << "\n";
        output << "hft_" << sanitized_name << "_histogram_sum " << stats.sum << "\n";
        
        // Also export key percentiles as separate gauges for alerting
        output << "# HELP hft_" << sanitized_name << "_p50_ns 50th percentile latency\n";
        output << "# TYPE hft_" << sanitized_name << "_p50_ns gauge\n";
        output << "hft_" << sanitized_name << "_p50_ns " << stats.p50 << "\n";
        
        output << "# HELP hft_" << sanitized_name << "_p99_ns 99th percentile latency\n";
        output << "# TYPE hft_" << sanitized_name << "_p99_ns gauge\n";
        output << "hft_" << sanitized_name << "_p99_ns " << stats.p99 << "\n";
    }
    
    static std::string sanitize_metric_name(const std::string& name) {
        std::string sanitized = name;
        
        // Replace invalid characters with underscores
        for (char& c : sanitized) {
            if (!std::isalnum(c) && c != '_') {
                c = '_';
            }
        }
        
        // Ensure it starts with a letter or underscore
        if (!std::isalpha(sanitized[0]) && sanitized[0] != '_') {
            sanitized = "_" + sanitized;
        }
        
        // Convert to lowercase
        std::transform(sanitized.begin(), sanitized.end(), sanitized.begin(), ::tolower);
        
        return sanitized;
    }
};

} // namespace hft