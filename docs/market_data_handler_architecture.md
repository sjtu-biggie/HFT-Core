# Market Data Handler Architecture

This document shows the complete architecture of the HFT Market Data Handler system with real-time Alpaca WebSocket integration.

## Architecture Diagram

```mermaid
graph TB
    %% External Data Sources
    subgraph "External Data Sources"
        ALPACA[Alpaca WebSocket API<br/>wss://stream.data.alpaca.markets/v2/iex]
        PCAP[PCAP Files<br/>Historical Data]
        DPDK[DPDK Network<br/>Kernel Bypass]
    end
    
    %% Market Data Handler Components
    subgraph "MarketDataHandler Process"
        subgraph "Data Source Layer"
            ALP_CLIENT[AlpacaMarketData<br/>Boost.Beast WebSocket Client]
            PCAP_READER[PCAPReader<br/>File Replay]
            DPDK_PROC[DPDK Processor<br/>Proof of Concept]
            MOCK_GEN[Mock Data Generator<br/>Realistic Market Simulation]
        end
        
        subgraph "Processing Layer"
            MDH_MAIN[MarketDataHandler<br/>Main Controller]
            MSG_PROC[Message Processing Thread]
            CTRL_PROC[Control Thread]
        end
        
        subgraph "Output Layer"
            ZMQ_PUB[ZeroMQ Publisher<br/>tcp://localhost:5556]
            METRICS[Metrics Publisher<br/>HFT Performance Tracking]
        end
    end
    
    %% Internal Message Flow
    subgraph "Message Types & Processing"
        subgraph "Alpaca Message Types"
            Q_MSG[Quote Messages 'q'<br/>bid/ask prices & sizes]
            T_MSG[Trade Messages 't'<br/>execution price & volume]
            B_MSG[Bar Messages 'b','d','u'<br/>OHLCV data]
            AUTH[Authentication Response<br/>connection status]
        end
        
        subgraph "Internal Format"
            MD_STRUCT[MarketData Struct<br/>- symbol[16]<br/>- bid_price, ask_price<br/>- bid_size, ask_size<br/>- last_price, last_size<br/>- exchange_timestamp]
        end
    end
    
    %% Consumer Services
    subgraph "Consumer Services"
        STRATEGY[Strategy Engine<br/>tcp://localhost:5556 subscriber]
        LOGGER[Low Latency Logger<br/>tcp://localhost:5555]
        WS_BRIDGE[WebSocket Bridge<br/>Real-time Dashboard]
    end
    
    %% Configuration & Control
    subgraph "Configuration & Control"
        CONFIG[StaticConfig<br/>hft_config.conf<br/>- API keys<br/>- Data source selection<br/>- Performance parameters]
        CTRL_API[Control API<br/>System Commands<br/>tcp://localhost:8080]
    end
    
    %% Data Flow Connections
    ALPACA -.->|SSL/TLS WebSocket| ALP_CLIENT
    PCAP --> PCAP_READER
    DPDK --> DPDK_PROC
    
    %% Alpaca Client Details
    ALP_CLIENT -->|JSON Array Messages| MSG_PROC
    Q_MSG --> MD_STRUCT
    T_MSG --> MD_STRUCT  
    B_MSG --> MD_STRUCT
    AUTH -.->|Status Updates| ALP_CLIENT
    
    %% Internal Processing
    PCAP_READER --> MSG_PROC
    DPDK_PROC --> MSG_PROC
    MOCK_GEN --> MSG_PROC
    MSG_PROC --> MDH_MAIN
    MDH_MAIN --> ZMQ_PUB
    
    %% Control Flow
    CTRL_API -->|Control Commands| CTRL_PROC
    CTRL_PROC --> MDH_MAIN
    CONFIG -.->|Configuration| MDH_MAIN
    CONFIG -.->|API Credentials| ALP_CLIENT
    
    %% Output Connections
    ZMQ_PUB -->|MarketData Messages| STRATEGY
    ZMQ_PUB -->|MarketData Messages| LOGGER
    ZMQ_PUB -->|MarketData Messages| WS_BRIDGE
    
    %% Metrics Flow
    MDH_MAIN --> METRICS
    ALP_CLIENT --> METRICS
    METRICS -.->|Performance Data| WS_BRIDGE
    
    %% Styling
    classDef external fill:#e1f5fe,stroke:#0277bd,stroke-width:2px
    classDef processor fill:#f3e5f5,stroke:#7b1fa2,stroke-width:2px
    classDef output fill:#e8f5e8,stroke:#388e3c,stroke-width:2px
    classDef control fill:#fff3e0,stroke:#f57c00,stroke-width:2px
    classDef message fill:#fce4ec,stroke:#c2185b,stroke-width:2px
    
    class ALPACA,PCAP,DPDK external
    class ALP_CLIENT,MDH_MAIN,MSG_PROC,CTRL_PROC,PCAP_READER,DPDK_PROC,MOCK_GEN processor
    class ZMQ_PUB,METRICS,STRATEGY,LOGGER,WS_BRIDGE output
    class CONFIG,CTRL_API control
    class Q_MSG,T_MSG,B_MSG,AUTH,MD_STRUCT message
```

## Key Components

### 1. External Data Sources
- **Alpaca WebSocket API**: Real-time market data via `wss://stream.data.alpaca.markets/v2/iex`
- **PCAP Files**: Historical market data replay capability
- **DPDK**: Proof-of-concept kernel bypass networking

### 2. AlpacaMarketData (Boost.Beast WebSocket Client)
- SSL/TLS encrypted connection with certificate validation
- Handles authentication flow with API key/secret
- Processes JSON array messages from Alpaca
- Supports quotes ('q'), trades ('t'), and bars ('b','d','u') message types
- Comprehensive error handling and metrics tracking

### 3. Message Processing Pipeline
- Multi-threaded processing for WebSocket I/O and control messages
- Converts external formats to internal MarketData struct
- Real-time performance tracking and latency measurements

### 4. ZeroMQ Publishing Infrastructure  
- High-performance messaging on tcp://localhost:5556
- Optimized for microsecond-level latency
- Supports multiple consumers (Strategy Engine, Logger, WebSocket Bridge)

### 5. Configuration & Control
- StaticConfig for compile-time optimization
- Runtime configuration via hft_config.conf
- Control API for system management

## Performance Characteristics
- **Target Latency**: <1ms end-to-end processing
- **Throughput**: >10,000 messages/second  
- **Error Handling**: Automatic reconnection and failover
- **Monitoring**: Comprehensive metrics and health checks

## Current Configuration
Based on `config/hft_config.conf`:
- Primary data source: `market_data.source=alpaca`
- WebSocket URL: `wss://stream.data.alpaca.markets/v2/iex`
- Fallback options: PCAP replay and mock data generation