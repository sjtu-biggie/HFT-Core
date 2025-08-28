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

// Market Data Processing Chain
constexpr const char* MD_RECEIVE_LATENCY = "md.receive_latency_ns";
constexpr const char* MD_PARSE_LATENCY = "md.parse_latency_ns";
constexpr const char* MD_NORMALIZE_LATENCY = "md.normalize_latency_ns";  
constexpr const char* MD_PUBLISH_LATENCY = "md.publish_latency_ns";
constexpr const char* MD_QUEUE_LATENCY = "md.queue_latency_ns";
constexpr const char* MD_TOTAL_LATENCY = "md.total_latency_ns";

// Strategy Engine Processing  
constexpr const char* STRATEGY_RECEIVE_LATENCY = "strategy.receive_latency_ns";
constexpr const char* STRATEGY_PROCESS_LATENCY = "strategy.process_latency_ns";
constexpr const char* STRATEGY_DECISION_LATENCY = "strategy.decision_latency_ns";
constexpr const char* STRATEGY_SIGNAL_LATENCY = "strategy.signal_latency_ns";
constexpr const char* STRATEGY_PUBLISH_LATENCY = "strategy.publish_latency_ns";
constexpr const char* STRATEGY_TOTAL_LATENCY = "strategy.total_latency_ns";

// Order Gateway Processing
constexpr const char* ORDER_RECEIVE_LATENCY = "order.receive_latency_ns";
constexpr const char* ORDER_VALIDATE_LATENCY = "order.validate_latency_ns";
constexpr const char* ORDER_RISK_CHECK_LATENCY = "order.risk_check_latency_ns";
constexpr const char* ORDER_SUBMIT_LATENCY = "order.submit_latency_ns";
constexpr const char* ORDER_ACK_LATENCY = "order.ack_latency_ns";
constexpr const char* ORDER_FILL_LATENCY = "order.fill_latency_ns";
constexpr const char* ORDER_TOTAL_LATENCY = "order.total_latency_ns";

// End-to-End Critical Path Latencies
constexpr const char* E2E_TICK_TO_SIGNAL = "e2e.tick_to_signal_ns";
constexpr const char* E2E_SIGNAL_TO_ORDER = "e2e.signal_to_order_ns";  
constexpr const char* E2E_TICK_TO_FILL = "e2e.tick_to_fill_ns";
constexpr const char* E2E_TICK_TO_ORDER = "e2e.tick_to_order_ns";

// =======================
// COMPONENT THROUGHPUT (Messages/Operations per second)
// =======================

// Market Data Handler
constexpr const char* MD_MESSAGES_RECEIVED = "md.messages_received_total";
constexpr const char* MD_MESSAGES_PROCESSED = "md.messages_processed_total";
constexpr const char* MD_MESSAGES_PUBLISHED = "md.messages_published_total";
constexpr const char* MD_MESSAGES_DROPPED = "md.messages_dropped_total";
constexpr const char* MD_MESSAGES_PER_SEC = "md.messages_per_second";
constexpr const char* MD_BYTES_RECEIVED = "md.bytes_received_total";

// Strategy Engine
constexpr const char* STRATEGY_SIGNALS_GENERATED = "strategy.signals_generated_total";
constexpr const char* STRATEGY_SIGNALS_PUBLISHED = "strategy.signals_published_total";  
constexpr const char* STRATEGY_DECISIONS_TOTAL = "strategy.decisions_total";
constexpr const char* STRATEGY_DECISIONS_PER_SEC = "strategy.decisions_per_second";
constexpr const char* STRATEGY_BUY_SIGNALS = "strategy.buy_signals_total";
constexpr const char* STRATEGY_SELL_SIGNALS = "strategy.sell_signals_total";

// Order Gateway
constexpr const char* ORDERS_RECEIVED = "orders.received_total";
constexpr const char* ORDERS_SUBMITTED = "orders.submitted_total";
constexpr const char* ORDERS_FILLED = "orders.filled_total";
constexpr const char* ORDERS_REJECTED = "orders.rejected_total";
constexpr const char* ORDERS_CANCELLED = "orders.cancelled_total";
constexpr const char* ORDERS_PER_SEC = "orders.per_second";

// Position & Risk Service
constexpr const char* POSITIONS_UPDATED = "positions.updated_total";
constexpr const char* RISK_CHECKS = "risk.checks_total";
constexpr const char* RISK_VIOLATIONS = "risk.violations_total";

// =======================
// TRADING PERFORMANCE METRICS
// =======================

// Position Tracking
constexpr const char* POSITIONS_OPEN = "trading.positions_open";
constexpr const char* POSITIONS_CLOSED = "trading.positions_closed_total";
constexpr const char* POSITION_SIZE = "trading.position_size";
constexpr const char* GROSS_EXPOSURE = "trading.gross_exposure_usd";
constexpr const char* NET_EXPOSURE = "trading.net_exposure_usd";

// P&L Metrics
constexpr const char* PNL_REALIZED = "trading.pnl_realized_usd";
constexpr const char* PNL_UNREALIZED = "trading.pnl_unrealized_usd";
constexpr const char* PNL_TOTAL = "trading.pnl_total_usd";
constexpr const char* PNL_DAY = "trading.pnl_day_usd";
constexpr const char* PNL_MTD = "trading.pnl_mtd_usd";

// Execution Quality
constexpr const char* FILL_RATE = "trading.fill_rate_percent";
constexpr const char* SLIPPAGE_BPS = "trading.slippage_bps";
constexpr const char* SPREAD_CAPTURE = "trading.spread_capture_bps";
constexpr const char* ADVERSE_SELECTION = "trading.adverse_selection_bps";
constexpr const char* AVG_FILL_SIZE = "trading.avg_fill_size";

// Risk Metrics
constexpr const char* VAR_1DAY = "risk.var_1day_usd";
constexpr const char* MAX_DRAWDOWN = "risk.max_drawdown_usd";
constexpr const char* SHARPE_RATIO = "risk.sharpe_ratio";
constexpr const char* LEVERAGE_RATIO = "risk.leverage_ratio";

// =======================
// SYSTEM PERFORMANCE METRICS  
// =======================

// Memory Usage (per component)
constexpr const char* MEMORY_RSS = "system.memory_rss_mb";
constexpr const char* MEMORY_VMS = "system.memory_vms_mb";
constexpr const char* MEMORY_HEAP = "system.memory_heap_mb";
constexpr const char* MEMORY_STACK = "system.memory_stack_mb";

// CPU Usage (per component and core) - avoid conflicts with metrics_collector.h
constexpr const char* HFT_CPU_USAGE = "system.cpu_usage_percent";
constexpr const char* CPU_CORE_USAGE = "system.cpu_core_usage_percent";
constexpr const char* CPU_CONTEXT_SWITCHES = "system.context_switches_total";
constexpr const char* CPU_CACHE_MISSES = "system.cache_misses_total";

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