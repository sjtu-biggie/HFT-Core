#pragma once

#include "metrics_collector.h"
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <chrono>
#include <iomanip>

namespace hft {

// Prometheus metrics exporter for HFT system
class PrometheusExporter {
public:
    static std::string export_metrics() {
        std::ostringstream prometheus_output;
        
        // Add system information
        add_system_info(prometheus_output);
        
        // Export collected metrics
        auto& collector = MetricsCollector::instance();
        auto stats = collector.get_statistics();
        
        for (const auto& [name, metric_stats] : stats) {
            export_metric(prometheus_output, name, metric_stats);
        }
        
        // Add custom HFT metrics
        add_hft_specific_metrics(prometheus_output);
        
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
        output << "# HELP hft_system_info HFT system build and version information\\n";
        output << "# TYPE hft_system_info gauge\\n";
        output << "hft_system_info{version=\\\"2.0\\\",build=\\\"" << __DATE__ << " " << __TIME__ << "\\\"} 1\\n";
        
        // Current timestamp
        output << "# HELP hft_scrape_timestamp_ms Timestamp of metrics scrape\\n";
        output << "# TYPE hft_scrape_timestamp_ms gauge\\n";
        output << "hft_scrape_timestamp_ms " << timestamp << "\\n";
        
        // Process information
        output << "# HELP hft_process_start_time_seconds Process start time in unix timestamp\\n";
        output << "# TYPE hft_process_start_time_seconds gauge\\n";
        output << "hft_process_start_time_seconds " << (timestamp / 1000.0) << "\\n";
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
        output << "# HELP hft_" << name << "_nanoseconds Latency measurements in nanoseconds\\n";
        output << "# TYPE hft_" << name << "_nanoseconds histogram\\n";
        
        // Define latency buckets (in nanoseconds)
        std::vector<uint64_t> buckets = {100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000};
        
        uint64_t cumulative_count = 0;
        for (uint64_t bucket : buckets) {
            // Estimate count in this bucket (simplified)
            uint64_t bucket_count = (stats.min_value <= bucket) ? stats.count : 0;
            cumulative_count += bucket_count;
            
            output << "hft_" << name << "_nanoseconds_bucket{le=\\\"" << bucket << "\\\"} " << cumulative_count << "\\n";
        }
        
        output << "hft_" << name << "_nanoseconds_bucket{le=\\\"+Inf\\\"} " << stats.count << "\\n";
        output << "hft_" << name << "_nanoseconds_count " << stats.count << "\\n";
        output << "hft_" << name << "_nanoseconds_sum " << (stats.sum) << "\\n";
        
        // Also export percentiles as separate gauges
        output << "# HELP hft_" << name << "_p50_nanoseconds 50th percentile latency\\n";
        output << "# TYPE hft_" << name << "_p50_nanoseconds gauge\\n";
        output << "hft_" << name << "_p50_nanoseconds " << stats.p50 << "\\n";
        
        output << "# HELP hft_" << name << "_p95_nanoseconds 95th percentile latency\\n";
        output << "# TYPE hft_" << name << "_p95_nanoseconds gauge\\n";
        output << "hft_" << name << "_p95_nanoseconds " << stats.p90 << "\\n";
        
        output << "# HELP hft_" << name << "_p99_nanoseconds 99th percentile latency\\n";
        output << "# TYPE hft_" << name << "_p99_nanoseconds gauge\\n";
        output << "hft_" << name << "_p99_nanoseconds " << stats.p99 << "\\n";
    }
    
    static void export_counter_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        output << "# HELP hft_" << name << "_total Total count of " << name << "\\n";
        output << "# TYPE hft_" << name << "_total counter\\n";
        output << "hft_" << name << "_total " << stats.sum << "\\n";
    }
    
    static void export_gauge_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        output << "# HELP hft_" << name << " Current value of " << name << "\\n";
        output << "# TYPE hft_" << name << " gauge\\n";
        output << "hft_" << name << " " << stats.max_value << "\\n";  // Use most recent max as current
        
        // Also export min/max if available
        if (stats.count > 0) {
            output << "# HELP hft_" << name << "_min Minimum observed value\\n";
            output << "# TYPE hft_" << name << "_min gauge\\n";
            output << "hft_" << name << "_min " << stats.min_value << "\\n";
            
            output << "# HELP hft_" << name << "_max Maximum observed value\\n";
            output << "# TYPE hft_" << name << "_max gauge\\n";
            output << "hft_" << name << "_max " << stats.max_value << "\\n";
        }
    }
    
    static void export_histogram_metric(std::ostringstream& output, const std::string& name, const MetricStats& stats) {
        output << "# HELP hft_" << name << " Distribution of " << name << "\\n";
        output << "# TYPE hft_" << name << " histogram\\n";
        
        // Simple histogram export (would need more sophisticated bucketing in production)
        output << "hft_" << name << "_bucket{le=\\\"+Inf\\\"} " << stats.count << "\\n";
        output << "hft_" << name << "_count " << stats.count << "\\n";
        output << "hft_" << name << "_sum " << stats.sum << "\\n";
    }
    
    static void add_hft_specific_metrics(std::ostringstream& output) {
        // Trading-specific metrics
        output << "# HELP hft_trades_executed_total Total number of trades executed\\n";
        output << "# TYPE hft_trades_executed_total counter\\n";
        output << "hft_trades_executed_total 0\\n";  // Would be populated from actual data
        
        output << "# HELP hft_positions_open Current number of open positions\\n";
        output << "# TYPE hft_positions_open gauge\\n";
        output << "hft_positions_open 0\\n";  // Would be populated from position service
        
        output << "# HELP hft_pnl_total_usd Total P&L in USD\\n";
        output << "# TYPE hft_pnl_total_usd gauge\\n";
        output << "hft_pnl_total_usd 0.0\\n";  // Would be populated from position service
        
        output << "# HELP hft_market_data_messages_received_total Market data messages received\\n";
        output << "# TYPE hft_market_data_messages_received_total counter\\n";
        output << "hft_market_data_messages_received_total 0\\n";
        
        output << "# HELP hft_orders_sent_total Orders sent to broker\\n";
        output << "# TYPE hft_orders_sent_total counter\\n";
        output << "hft_orders_sent_total 0\\n";
        
        output << "# HELP hft_system_status System operational status (1=healthy, 0=degraded)\\n";
        output << "# TYPE hft_system_status gauge\\n";
        output << "hft_system_status{component=\\\"market_data\\\"} 1\\n";
        output << "hft_system_status{component=\\\"strategy_engine\\\"} 1\\n";
        output << "hft_system_status{component=\\\"order_gateway\\\"} 1\\n";
        output << "hft_system_status{component=\\\"position_service\\\"} 1\\n";
        
        // Network and system metrics
        output << "# HELP hft_network_packets_dropped_total Network packets dropped\\n";
        output << "# TYPE hft_network_packets_dropped_total counter\\n";
        output << "hft_network_packets_dropped_total 0\\n";
        
        output << "# HELP hft_memory_usage_bytes Memory usage in bytes\\n";
        output << "# TYPE hft_memory_usage_bytes gauge\\n";
        output << "hft_memory_usage_bytes 0\\n";  // Would get from system
        
        output << "# HELP hft_cpu_usage_percent CPU usage percentage\\n";
        output << "# TYPE hft_cpu_usage_percent gauge\\n";
        output << "hft_cpu_usage_percent 0.0\\n";  // Would get from system
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