#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>

#ifdef DPDK_ENABLED
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_pcapng.h>
#endif

namespace hft {

// Ethernet + IP + UDP header sizes for market data parsing
constexpr size_t ETH_HEADER_SIZE = 14;
constexpr size_t IP_HEADER_SIZE = 20;
constexpr size_t UDP_HEADER_SIZE = 8;
constexpr size_t MIN_MARKET_DATA_PACKET_SIZE = ETH_HEADER_SIZE + IP_HEADER_SIZE + UDP_HEADER_SIZE + 32;

// Market data feed formats
enum class FeedFormat {
    NASDAQ_ITCH_5_0,    // NASDAQ ITCH 5.0 protocol
    NYSE_PILLAR,        // NYSE Pillar feed
    IEX_TOPS,           // IEX TOPS feed
    FIX_PROTOCOL,       // FIX protocol messages
    GENERIC_CSV         // Generic CSV format for testing
};

// NASDAQ ITCH message types
struct ITCHMessageHeader {
    uint16_t length;
    uint8_t message_type;
} __attribute__((packed));

// Basic market data packet structure
struct MarketDataPacket {
    std::chrono::nanoseconds timestamp;
    std::string symbol;
    double bid_price = 0.0;
    double ask_price = 0.0;
    uint32_t bid_size = 0;
    uint32_t ask_size = 0;
    double last_price = 0.0;
    uint32_t last_size = 0;
    FeedFormat format;
};

class PCAPReader {
public:
    PCAPReader(const std::string& pcap_file, FeedFormat format = FeedFormat::GENERIC_CSV);
    ~PCAPReader();
    
    // Initialize PCAP reader and optionally DPDK
    bool initialize(bool use_dpdk = false);
    
    // Start reading packets
    void start_reading();
    void stop_reading();
    bool is_reading() const { return reading_; }
    
    // Set callback for processed market data
    void set_data_callback(std::function<void(const MarketData&)> callback);
    
    // Replay control
    void set_replay_speed(double speed_multiplier) { replay_speed_ = speed_multiplier; }
    void set_loop_replay(bool loop) { loop_replay_ = loop; }
    
    // Statistics
    uint64_t get_packets_processed() const { return packets_processed_; }
    uint64_t get_packets_parsed() const { return packets_parsed_; }
    uint64_t get_parse_errors() const { return parse_errors_; }

private:
    std::string pcap_file_;
    FeedFormat feed_format_;
    bool reading_ = false;
    bool use_dpdk_ = false;
    double replay_speed_ = 1.0;
    bool loop_replay_ = false;
    
    // Statistics
    std::atomic<uint64_t> packets_processed_{0};
    std::atomic<uint64_t> packets_parsed_{0};
    std::atomic<uint64_t> parse_errors_{0};
    
    // Callback for processed data
    std::function<void(const MarketData&)> data_callback_;
    
    // Processing thread
    std::unique_ptr<std::thread> processing_thread_;
    std::atomic<bool> should_stop_{false};
    
    Logger logger_;
    
    // Core processing methods
    void process_pcap_file();
    bool process_packet(const uint8_t* packet_data, size_t packet_len, uint64_t timestamp_ns);
    
    // Protocol parsers
    bool parse_nasdaq_itch(const uint8_t* payload, size_t len, MarketDataPacket& packet);
    bool parse_nyse_pillar(const uint8_t* payload, size_t len, MarketDataPacket& packet);
    bool parse_iex_tops(const uint8_t* payload, size_t len, MarketDataPacket& packet);
    bool parse_fix_protocol(const uint8_t* payload, size_t len, MarketDataPacket& packet);
    bool parse_generic_csv(const uint8_t* payload, size_t len, MarketDataPacket& packet);
    
    // Network parsing helpers
    bool extract_udp_payload(const uint8_t* packet, size_t len, const uint8_t*& payload, size_t& payload_len);
    std::string parse_symbol(const char* symbol_data, size_t max_len);
    double parse_price_field(uint64_t price_int, uint32_t decimal_places = 4);
    
    // Convert to internal format
    MarketData convert_to_market_data(const MarketDataPacket& packet);
    
    // DPDK-specific methods (if enabled)
#ifdef DPDK_ENABLED
    bool initialize_dpdk_pcap();
    void process_dpdk_pcap();
    struct rte_pcapng* pcapng_reader_ = nullptr;
#endif
};

} // namespace hft