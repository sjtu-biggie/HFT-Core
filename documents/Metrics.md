# HFT System Metrics Architecture

## Overview

The HFT system implements a comprehensive, high-performance metrics collection and monitoring architecture designed for sub-microsecond latency trading systems. The architecture consists of three main layers:

1. **Collection Layer**: Thread-local, lock-free metrics collection
2. **Processing Layer**: Aggregation and statistical analysis 
3. **Export Layer**: Prometheus-compatible metrics export to Grafana Cloud

## Architecture Components

### 1. MetricsCollector (Core Collection Engine)

**Location**: `src/common/metrics_collector.cpp`

**Key Features**:
- **Thread-local storage**: Each thread has its own metrics buffer to avoid contention
- **Lock-free ring buffers**: High-performance circular buffers for metrics storage
- **RDTSC-based timing**: CPU cycle-accurate latency measurements
- **Automatic aggregation**: Background thread collects from all thread buffers every 100ms

**Supported Metric Types**:
```cpp
enum class MetricType {
    LATENCY,     // Nanosecond-precision timing
    COUNTER,     // Monotonic counters
    GAUGE,       // Current value measurements  
    HISTOGRAM    // Value distribution tracking
}
```

### 2. HFT Metrics Framework (Domain-Specific Metrics)

**Location**: `src/common/hft_metrics.h`, `src/common/hft_metrics.cpp`

**Critical Trading Metrics Categories**:

#### A. Critical Path Latencies (nanosecond precision)
```cpp
// End-to-end trading latencies
e2e.tick_to_signal_ns     // Market data ’ trading signal
e2e.tick_to_order_ns      // Market data ’ order submission  
e2e.tick_to_fill_ns       // Market data ’ order fill

// Component-specific latencies
md.total_latency_ns       // Market data processing
strategy.total_latency_ns // Strategy decision time
order.total_latency_ns    // Order processing time
```

#### B. Throughput Metrics
```cpp
md.messages_per_second         // Market data rate
strategy.decisions_per_second  // Strategy signal rate
orders.per_second             // Order submission rate
```

#### C. Trading Performance
```cpp
trading.positions_open        // Current open positions
trading.pnl_total_usd        // Total P&L in USD
trading.fill_rate_percent    // Order fill success rate
trading.slippage_bps         // Execution slippage
```

#### D. System Health
```cpp
system.memory_rss_mb         // Memory usage
system.cpu_usage_percent     // CPU utilization
system.thread_count          // Active threads
network.bytes_received_total // Network traffic
```

### 3. Prometheus Exporter (Standards-Compatible Export)

**Location**: `src/common/prometheus_exporter.h`

**Features**:
- **Prometheus format compliance**: Text exposition format v0.0.4
- **HFT-optimized histogram buckets**: Sub-microsecond latency buckets
- **Automatic metric sanitization**: Name conversion for Prometheus compatibility
- **Comprehensive help text**: Self-documenting metrics

**Example Output**:
```prometheus
# HELP hft_e2e_tick_to_order_ns_p99_ns 99th percentile latency
# TYPE hft_e2e_tick_to_order_ns_p99_ns gauge
hft_e2e_tick_to_order_ns_p99_ns 750000

# HELP hft_trading_pnl_total_usd Total P&L in USD
# TYPE hft_trading_pnl_total_usd gauge
hft_trading_pnl_total_usd 1250.75
```

### 4. WebSocket Bridge (HTTP Metrics Endpoint)

**Location**: `src/websocket_bridge/websocket_bridge.cpp`

**Endpoints**:
- **`http://localhost:8080/metrics`**: Prometheus metrics endpoint
- **`http://localhost:8080/`**: Real-time JSON data feed

**Features**:
- **Thread pool architecture**: 8 worker threads for client handling
- **CORS-enabled**: Cross-origin support for web dashboards
- **Connection limiting**: Max 100 concurrent connections
- **Automatic timeouts**: 5-second client timeout for security

### 5. System Resource Monitor

**Location**: `src/common/hft_metrics.cpp` (SystemResourceMonitor class)

**Monitored Resources**:
- **Memory**: RSS, VMS, heap usage from `/proc/self/status`
- **CPU**: Usage percentage, context switches from `/proc/stat`
- **Network**: Bytes/packets sent/received from `/proc/net/dev`  
- **Threads**: Active thread count from `/proc/self/task`

**Update Frequency**: 1 second intervals via background thread

## Integration Points

### Component Integration

Each HFT component initializes metrics collection:

```cpp
// Market Data Handler
MarketDataHandler::MarketDataHandler()
    : throughput_tracker_(hft::metrics::MD_MESSAGES_RECEIVED, 
                         hft::metrics::MD_MESSAGES_PER_SEC) {
    // Initialize metrics
}

// Strategy Engine  
void MomentumStrategy::on_market_data(const MarketData& data) {
    HFT_RDTSC_TIMER(metrics::STRATEGY_PROCESS_LATENCY);  // Auto timing
    // Strategy logic
    HFT_COMPONENT_COUNTER(metrics::STRATEGY_SIGNALS_GENERATED);
}
```

### Macros for Easy Integration

```cpp
#define HFT_RDTSC_TIMER(label)     // RDTSC-based automatic timing
#define HFT_COMPONENT_COUNTER(name)  // Increment counter
#define HFT_LATENCY_NS(label, ns)    // Record latency value
#define HFT_GAUGE_VALUE(label, value) // Set gauge value
```

## Prometheus & Grafana Cloud Setup

### Local Prometheus Configuration

**File**: `config/prometheus-grafana-cloud.yml`

**Key Settings**:
```yaml
global:
  scrape_interval: 5s      # Fast scraping for HFT
  scrape_timeout: 2s       # Quick timeout

remote_write:
  - url: "YOUR_GRAFANA_CLOUD_ENDPOINT"
    basic_auth:
      username: "YOUR_USERNAME" 
      password: "YOUR_PASSWORD"
    
    # Optional: Only send critical metrics to reduce costs
    write_relabel_configs:
      - source_labels: [__name__]
        regex: 'hft_(e2e_tick_to_order_ns|trading_pnl_total_usd|component_status).*'
        action: keep
```

**Scrape Targets**:
- **Port 8080**: WebSocket Bridge (all HFT metrics)
- **Port 8081**: Control API (system control metrics)

### Grafana Dashboard

**File**: `config/hft_grafana_dashboard.json`

**Dashboard Panels**:
1. **Critical Path Latencies**: P50, P95, P99 tick-to-order times
2. **Component Health**: Service status indicators
3. **Throughput Metrics**: Messages/orders per second
4. **Trading Performance**: P&L, fill rates, positions
5. **System Resources**: CPU, memory, network usage

## Performance Characteristics

### Collection Performance
- **Thread-local buffers**: Zero contention between trading threads
- **Ring buffer capacity**: 4096 entries per thread by default
- **Collection frequency**: 100ms aggregation intervals
- **Memory overhead**: ~1MB per active thread

### Latency Targets
- **Metric recording**: <50ns per measurement
- **Buffer overflow handling**: Lock-free overwrite of oldest data
- **Export generation**: <1ms for full metrics set

### Throughput Capacity
- **Sustained rate**: >1M metrics/second per thread
- **Burst capacity**: >10M metrics/second across all threads
- **Export rate**: 5-second Prometheus scraping intervals

## Monitoring Best Practices

### Critical Alerts (from `config/hft_alerts.yml`)

```yaml
# P99 latency exceeded 1ms
- alert: HFT_HighLatency_P99
  expr: hft_e2e_tick_to_order_ns_p99_ns > 1000000
  for: 5s

# Component offline
- alert: HFT_ComponentDown  
  expr: hft_component_status == 0
  for: 2s

# Large trading loss
- alert: HFT_LargeLoss
  expr: hft_trading_pnl_total_usd < -10000
  for: 0s  # Immediate alert
```

### Key Performance Indicators

1. **Latency KPIs**:
   - P99 tick-to-order < 1ms (critical)
   - P95 tick-to-order < 500¼s (warning)
   - Strategy processing < 100¼s

2. **Throughput KPIs**:
   - Market data rate > 1000 msg/sec
   - Order fill rate > 95%
   - System CPU < 80%

3. **Business KPIs**:
   - Daily P&L tracking
   - Position limits monitoring
   - Slippage < 5 basis points

## Starting the Metrics System

### Manual Process

```bash
# 1. Start Prometheus (with Grafana Cloud config)
./scripts/start_prometheus.sh

# 2. Start HFT services (they auto-initialize metrics)
./build/websocket_bridge    # Metrics endpoint
./build/market_data_handler # Generates MD metrics
./build/strategy_engine     # Generates strategy metrics
./build/order_gateway       # Generates trading metrics
```

### Verification

```bash
# Check metrics endpoint
curl http://localhost:8080/metrics

# Check Prometheus targets
curl http://localhost:9090/api/v1/targets

# Verify data in Grafana Cloud dashboard
```

## Architecture Benefits

1. **Sub-microsecond Precision**: RDTSC-based timing for true HFT performance
2. **Zero Trading Path Impact**: Lock-free collection doesn't affect latency
3. **Industrial Standards**: Prometheus/Grafana ecosystem compatibility  
4. **Comprehensive Coverage**: End-to-end visibility from network to P&L
5. **Cloud Integration**: Grafana Cloud for enterprise monitoring
6. **Cost Optimization**: Selective metric forwarding to reduce cloud costs

## Future Enhancements

1. **Hardware Integration**: FPGA timestamp correlation
2. **ML Anomaly Detection**: Grafana Cloud ML for pattern recognition
3. **Real-time Alerting**: Sub-second alert propagation
4. **Cross-venue Metrics**: Multi-exchange latency comparison
5. **Regulatory Reporting**: Automated compliance metric generation

This architecture provides the foundation for professional-grade HFT system monitoring with nanosecond precision and industrial-scale observability.