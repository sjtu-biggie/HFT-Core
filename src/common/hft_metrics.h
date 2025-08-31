#pragma once

#include "metrics_collector.h"
#include "high_res_timer.h"
#include <cstdint>

namespace hft {

// Comprehensive HFT Metrics Specification
// This file defines all critical metrics for high-frequency trading systems
// Categories: Latency, Throughput, Trading, Risk, System, Network

namespace metrics {

// =======================
// CRITICAL PATH LATENCIES (RDTSC-based, nanosecond precision)
// =======================

// Market Data Processing Chain - remove prefixes, use service labels
constexpr const char* PARSE_LATENCY = "parse_latency_ns";
constexpr const char* PROCESS_LATENCY = "process_latency_ns";
constexpr const char* PUBLISH_LATENCY = "publish_latency_ns";
constexpr const char* TOTAL_LATENCY = "total_latency_ns";
constexpr const char* RISK_CHECK_LATENCY = "risk_check_latency_ns";
constexpr const char* SUBMIT_LATENCY = "submit_latency_ns";

constexpr const char* MD_PARSE_LATENCY = PARSE_LATENCY;
constexpr const char* MD_PUBLISH_LATENCY = PUBLISH_LATENCY;
constexpr const char* MD_TOTAL_LATENCY = TOTAL_LATENCY;

constexpr const char* STRATEGY_PROCESS_LATENCY = PROCESS_LATENCY;
constexpr const char* STRATEGY_PUBLISH_LATENCY = PUBLISH_LATENCY;
constexpr const char* STRATEGY_TOTAL_LATENCY = TOTAL_LATENCY;

constexpr const char* ORDER_RISK_CHECK_LATENCY = RISK_CHECK_LATENCY;
constexpr const char* ORDER_PROCESS_LATENCY = PROCESS_LATENCY;               // Missing in strategy engine  
constexpr const char* ORDER_TOTAL_LATENCY = TOTAL_LATENCY;

constexpr const char* MARKET_DATA_MESSAGES = "market_data_messages_total";   // Missing in strategy engine
constexpr const char* SIGNALS_GENERATED = "signals_generated_total";        // Missing in strategy engine
constexpr const char* TICK_TO_SIGNAL = "tick_to_signal_ns";
constexpr const char* SIGNAL_TO_ORDER = "signal_to_order_ns";  
constexpr const char* TICK_TO_FILL = "tick_to_fill_ns";
constexpr const char* TICK_TO_ORDER = "tick_to_order_ns";

// Backward compatibility - deprecated, use above constants
constexpr const char* E2E_TICK_TO_SIGNAL = "tick_to_signal_ns";
constexpr const char* E2E_SIGNAL_TO_ORDER = "signal_to_order_ns";  
constexpr const char* E2E_TICK_TO_FILL = "tick_to_fill_ns";
constexpr const char* E2E_TICK_TO_ORDER = "tick_to_order_ns";

// =======================
// COMPONENT THROUGHPUT (Messages/Operations per second)
// =======================

// Message Counters - remove prefixes, use service labels
constexpr const char* MESSAGES_RECEIVED = "messages_received_total";
constexpr const char* MESSAGES_PROCESSED = "messages_processed_total";
constexpr const char* MESSAGES_PUBLISHED = "messages_published_total";
constexpr const char* MESSAGES_DROPPED = "messages_dropped_total";
constexpr const char* MESSAGES_PER_SECOND = "messages_per_second";
constexpr const char* BYTES_RECEIVED_TOTAL = "bytes_received_total";

// Backward compatibility - deprecated, use above constants
constexpr const char* MD_MESSAGES_RECEIVED = "messages_received_total";
constexpr const char* MD_MESSAGES_PROCESSED = "messages_processed_total";
constexpr const char* MD_MESSAGES_PUBLISHED = "messages_published_total";
constexpr const char* MD_MESSAGES_DROPPED = "messages_dropped_total";
constexpr const char* MD_MESSAGES_PER_SEC = "messages_per_second";
constexpr const char* MD_BYTES_RECEIVED = "bytes_received_total";

// Signal Counters - remove prefixes, use service labels
constexpr const char* BUY_SIGNALS = "buy_signals_total";
constexpr const char* SELL_SIGNALS = "sell_signals_total";

// Backward compatibility - deprecated, use above constants
constexpr const char* STRATEGY_DECISIONS_TOTAL = "decisions_total";
constexpr const char* STRATEGY_DECISIONS_PER_SEC = "decisions_per_second";
constexpr const char* STRATEGY_BUY_SIGNALS = "buy_signals_total";
constexpr const char* STRATEGY_SELL_SIGNALS = "sell_signals_total";

// Order Counters - remove prefixes, use service labels
constexpr const char* ORDERS_RECEIVED_TOTAL = "orders_received_total";
constexpr const char* ORDERS_SUBMITTED_TOTAL = "orders_submitted_total";
constexpr const char* ORDERS_FILLED_TOTAL = "orders_filled_total";
constexpr const char* ORDERS_REJECTED_TOTAL = "orders_rejected_total";
constexpr const char* ORDERS_CANCELLED_TOTAL = "orders_cancelled_total";
constexpr const char* ORDERS_PER_SECOND = "orders_per_second";

// Backward compatibility - deprecated, use above constants
constexpr const char* ORDERS_RECEIVED = "orders_received_total";
constexpr const char* ORDERS_SUBMITTED = "orders_submitted_total";
constexpr const char* ORDERS_FILLED = "orders_filled_total";
constexpr const char* ORDERS_REJECTED = "orders_rejected_total";
constexpr const char* ORDERS_CANCELLED = "orders_cancelled_total";
constexpr const char* ORDERS_PER_SEC = "orders_per_second";

// Position & Risk Counters - remove prefixes, use service labels
constexpr const char* POSITIONS_UPDATED_TOTAL = "positions_updated_total";
constexpr const char* RISK_CHECKS_TOTAL = "risk_checks_total";
constexpr const char* RISK_VIOLATIONS_TOTAL = "risk_violations_total";

// Backward compatibility - deprecated, use above constants
constexpr const char* POSITIONS_UPDATED = "positions_updated_total";
constexpr const char* RISK_CHECKS = "risk_checks_total";
constexpr const char* RISK_VIOLATIONS = "risk_violations_total";

// =======================
// TRADING PERFORMANCE METRICS
// =======================

// Position Tracking - remove prefixes, use service labels
constexpr const char* POSITIONS_OPEN_COUNT = "positions_open";
constexpr const char* POSITIONS_CLOSED_TOTAL = "positions_closed_total";
constexpr const char* POSITION_SIZE_CURRENT = "position_size";
constexpr const char* GROSS_EXPOSURE_USD = "gross_exposure_usd";
constexpr const char* NET_EXPOSURE_USD = "net_exposure_usd";

// Backward compatibility - deprecated, use above constants
constexpr const char* POSITIONS_OPEN = "positions_open";
constexpr const char* POSITIONS_CLOSED = "positions_closed_total";
constexpr const char* POSITION_SIZE = "position_size";
constexpr const char* GROSS_EXPOSURE = "gross_exposure_usd";
constexpr const char* NET_EXPOSURE = "net_exposure_usd";

// P&L Metrics - remove prefixes, use service labels
constexpr const char* PNL_REALIZED_USD = "pnl_realized_usd";
constexpr const char* PNL_UNREALIZED_USD = "pnl_unrealized_usd";
constexpr const char* PNL_TOTAL_USD = "pnl_total_usd";
constexpr const char* PNL_DAY_USD = "pnl_day_usd";
constexpr const char* PNL_MTD_USD = "pnl_mtd_usd";

// Backward compatibility - deprecated, use above constants
constexpr const char* PNL_REALIZED = "pnl_realized_usd";
constexpr const char* PNL_UNREALIZED = "pnl_unrealized_usd";
constexpr const char* PNL_TOTAL = "pnl_total_usd";
constexpr const char* PNL_DAY = "pnl_day_usd";
constexpr const char* PNL_MTD = "pnl_mtd_usd";

// Execution Quality - remove prefixes, use service labels
constexpr const char* FILL_RATE_PERCENT = "fill_rate_percent";
constexpr const char* SLIPPAGE_BPS_AVG = "slippage_bps";
constexpr const char* SPREAD_CAPTURE_BPS = "spread_capture_bps";
constexpr const char* ADVERSE_SELECTION_BPS = "adverse_selection_bps";
constexpr const char* AVG_FILL_SIZE_SHARES = "avg_fill_size";

// Backward compatibility - deprecated, use above constants
constexpr const char* FILL_RATE = "fill_rate_percent";
constexpr const char* SLIPPAGE_BPS = "slippage_bps";
constexpr const char* SPREAD_CAPTURE = "spread_capture_bps";
constexpr const char* ADVERSE_SELECTION = "adverse_selection_bps";
constexpr const char* AVG_FILL_SIZE = "avg_fill_size";

// Risk Metrics
constexpr const char* VAR_1DAY = "risk.var_1day_usd";
constexpr const char* MAX_DRAWDOWN = "risk.max_drawdown_usd";
constexpr const char* SHARPE_RATIO = "risk.sharpe_ratio";
constexpr const char* LEVERAGE_RATIO = "risk.leverage_ratio";

// =======================
// SYSTEM PERFORMANCE METRICS  
// =======================

// Memory Usage - remove prefixes, use service labels
constexpr const char* MEMORY_RSS_MB = "memory_rss_mb";
constexpr const char* MEMORY_VMS_MB = "memory_vms_mb";
constexpr const char* MEMORY_HEAP_MB = "memory_heap_mb";
constexpr const char* MEMORY_STACK_MB = "memory_stack_mb";

// Backward compatibility - deprecated, use above constants
constexpr const char* MEMORY_RSS = "memory_rss_mb";
constexpr const char* MEMORY_VMS = "memory_vms_mb";
constexpr const char* MEMORY_HEAP = "memory_heap_mb";
constexpr const char* MEMORY_STACK = "memory_stack_mb";

// CPU Usage - remove prefixes, use service labels
constexpr const char* CPU_USAGE_PERCENT = "cpu_usage_percent";
constexpr const char* CPU_CORE_USAGE_PERCENT = "cpu_core_usage_percent";
constexpr const char* CPU_CONTEXT_SWITCHES_TOTAL = "context_switches_total";
constexpr const char* CPU_CACHE_MISSES_TOTAL = "cache_misses_total";

// Backward compatibility - deprecated, use above constants
constexpr const char* CPU_USAGE = "cpu_usage_percent";
constexpr const char* CPU_CORE_USAGE = "cpu_core_usage_percent";
constexpr const char* CPU_CONTEXT_SWITCHES = "context_switches_total";
constexpr const char* CPU_CACHE_MISSES = "cache_misses_total";

// Threading and Concurrency
constexpr const char* THREAD_COUNT = "system.thread_count";
constexpr const char* LOCK_CONTENTION = "system.lock_contention_ns";
constexpr const char* HFT_QUEUE_DEPTH = "system.queue_depth";
constexpr const char* QUEUE_FULL_EVENTS = "system.queue_full_events_total";

// Garbage Collection (if applicable)
constexpr const char* GC_COLLECTIONS = "system.gc_collections_total";
constexpr const char* GC_TIME = "system.gc_time_ms";
constexpr const char* GC_ALLOCATED = "system.gc_allocated_mb";

// =======================
// NETWORK PERFORMANCE METRICS
// =======================

// Network I/O
constexpr const char* NETWORK_BYTES_SENT = "network.bytes_sent_total";
constexpr const char* NETWORK_BYTES_RECV = "network.bytes_received_total";
constexpr const char* NETWORK_PACKETS_SENT = "network.packets_sent_total";
constexpr const char* NETWORK_PACKETS_RECV = "network.packets_received_total";
constexpr const char* NETWORK_ERRORS = "network.errors_total";
constexpr const char* NETWORK_DROPS = "network.drops_total";

// Connection Health
constexpr const char* CONNECTION_COUNT = "network.connections_active";
constexpr const char* CONNECTION_ESTABLISHED = "network.connections_established_total";
constexpr const char* CONNECTION_CLOSED = "network.connections_closed_total";
constexpr const char* CONNECTION_FAILED = "network.connections_failed_total";

// Protocol-Specific
constexpr const char* ZMQ_MESSAGES_SENT = "zmq.messages_sent_total";
constexpr const char* ZMQ_MESSAGES_RECV = "zmq.messages_received_total";
constexpr const char* ZMQ_SOCKET_ERRORS = "zmq.socket_errors_total";
constexpr const char* TCP_RETRANSMITS = "tcp.retransmits_total";

// =======================
// DATA QUALITY METRICS
// =======================

// Market Data Quality
constexpr const char* MD_GAPS = "data.md_gaps_total";
constexpr const char* MD_LATE_ARRIVALS = "data.md_late_arrivals_total";
constexpr const char* MD_OUT_OF_ORDER = "data.md_out_of_order_total";
constexpr const char* MD_STALE_QUOTES = "data.md_stale_quotes_total";
constexpr const char* MD_FEED_LATENCY = "data.md_feed_latency_ms";

// Data Integrity
constexpr const char* DATA_CORRUPTION = "data.corruption_events_total";
constexpr const char* DATA_VALIDATION_FAILURES = "data.validation_failures_total";
constexpr const char* SEQUENCE_GAPS = "data.sequence_gaps_total";

// =======================
// BUSINESS LOGIC METRICS
// =======================

// Strategy Performance
constexpr const char* STRATEGY_WIN_RATE = "strategy.win_rate_percent";
constexpr const char* STRATEGY_PROFIT_FACTOR = "strategy.profit_factor";
constexpr const char* STRATEGY_MAX_DD = "strategy.max_drawdown_usd";
constexpr const char* STRATEGY_TRADES_TODAY = "strategy.trades_today";

// Market Making (if applicable)
constexpr const char* MM_QUOTES_SENT = "mm.quotes_sent_total";
constexpr const char* MM_QUOTES_HIT = "mm.quotes_hit_total";
constexpr const char* MM_INVENTORY = "mm.inventory_shares";
constexpr const char* MM_SPREAD = "mm.spread_bps";

// Arbitrage (if applicable)
constexpr const char* ARB_OPPORTUNITIES = "arb.opportunities_total";
constexpr const char* ARB_EXECUTED = "arb.executed_total";
constexpr const char* ARB_PROFIT = "arb.profit_usd";

// =======================
// COMPONENT HEALTH STATUS
// =======================

constexpr const char* COMPONENT_STATUS = "health.component_status";
constexpr const char* SERVICE_UPTIME = "health.uptime_seconds";
constexpr const char* HEARTBEAT = "health.heartbeat_timestamp";
constexpr const char* ERROR_RATE = "health.error_rate_percent";
constexpr const char* WARNING_COUNT = "health.warnings_total";

} // namespace metrics

// RDTSC-based high precision timer for latency measurements
class RDTSCTimer {
private:
    uint64_t start_ticks_;
    const char* label_;
    
public:
    explicit RDTSCTimer(const char* label) : label_(label) {
        start_ticks_ = __builtin_ia32_rdtsc();
    }
    
    ~RDTSCTimer() {
        uint64_t end_ticks = __builtin_ia32_rdtsc();
        uint64_t elapsed_ticks = end_ticks - start_ticks_;
        
        // Bounds checking to prevent overflow and invalid values
        if (elapsed_ticks > 0 && elapsed_ticks < UINT64_MAX / 1000) {
            // Convert TSC to nanoseconds with safer arithmetic
            // Use conservative 2.5GHz base frequency to avoid division by zero
            // and handle various CPU speeds more safely
            uint64_t elapsed_ns = (elapsed_ticks * 1000) / 2500; // ~2.5GHz assumption
            
            // Sanity check: if result seems unreasonable (>1 second), cap it
            if (elapsed_ns > 1000000000ULL) {
                elapsed_ns = 1000000000ULL; // Cap at 1 second
            }
            
            MetricsCollector::instance().record_latency(label_, elapsed_ns);
        }
        // If elapsed_ticks is invalid, we simply don't record the metric
    }
};

// Component throughput tracker - declaration only
class ComponentThroughput {
private:
    const char* counter_name_;
    const char* rate_name_;
    uint64_t last_count_;
    uint64_t last_timestamp_;
    
public:
    ComponentThroughput(const char* counter_name, const char* rate_name);
    void increment(uint64_t count = 1);
};

// System resource monitor
class SystemResourceMonitor {
public:
    static void update_memory_usage();
    static void update_cpu_usage();
    static void update_network_stats();
    static void update_thread_stats();
};

// HFT Metrics System Initialization
void initialize_hft_metrics();
void shutdown_hft_metrics();

// Convenient macros for HFT metrics
#define HFT_RDTSC_TIMER(label) hft::RDTSCTimer _rdtsc_timer(label)
#define HFT_COMPONENT_COUNTER(name) hft::MetricsCollector::instance().increment_counter(name)
#define HFT_LATENCY_NS(label, ns) hft::MetricsCollector::instance().record_latency(label, ns)
#define HFT_GAUGE_VALUE(label, value) hft::MetricsCollector::instance().set_gauge(label, value)

} // namespace hft