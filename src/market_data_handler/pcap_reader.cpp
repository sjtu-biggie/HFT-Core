#include "pcap_reader.h"
#include "../common/static_config.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include <cmath>

#ifdef HAS_PCAP
#include <pcap/pcap.h>
#endif

namespace hft {

PCAPReader::PCAPReader(const std::string& pcap_file, FeedFormat format)
    : pcap_file_(pcap_file)
    , feed_format_(format)
    , logger_("PCAPReader", StaticConfig::get_logger_endpoint()) {
    logger_.info("PCAPReader initialized for file: " + pcap_file);
}

PCAPReader::~PCAPReader() {
    stop_reading();
}

bool PCAPReader::initialize(bool use_dpdk) {
    use_dpdk_ = use_dpdk;
    
#ifndef HAS_PCAP
    logger_.error("PCAP support not available. Install libpcap-dev and rebuild");
    return false;
#endif
    
    // Check if PCAP file exists
    std::ifstream file(pcap_file_);
    if (!file.good()) {
        logger_.error("PCAP file not found: " + pcap_file_);
        return false;
    }
    
#ifdef DPDK_ENABLED
    if (use_dpdk_) {
        return initialize_dpdk_pcap();
    }
#else
    if (use_dpdk_) {
        logger_.warning("DPDK not available, falling back to libpcap");
        use_dpdk_ = false;
    }
#endif
    
    logger_.info("PCAP reader initialized with libpcap backend");
    return true;
}

void PCAPReader::start_reading() {
    if (reading_) {
        logger_.warning("PCAP reader is already running");
        return;
    }
    
    reading_ = true;
    should_stop_ = false;
    processing_thread_ = std::make_unique<std::thread>(&PCAPReader::process_pcap_file, this);
    logger_.info("PCAP reader started");
}

void PCAPReader::stop_reading() {
    if (!reading_) return;
    
    logger_.info("Stopping PCAP reader");
    should_stop_ = true;
    reading_ = false;
    
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    
    logger_.info("PCAP reader stopped");
}

void PCAPReader::set_data_callback(std::function<void(const MarketData&)> callback) {
    data_callback_ = callback;
}

void PCAPReader::process_pcap_file() {
    logger_.info("Starting PCAP file processing thread");
    
#ifndef HAS_PCAP
    logger_.error("PCAP support not available");
    reading_ = false;
    return;
#endif

#ifdef HAS_PCAP
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t* pcap_handle = pcap_open_offline(pcap_file_.c_str(), errbuf);
    
    if (!pcap_handle) {
        logger_.error("Failed to open PCAP file: " + std::string(errbuf));
        reading_ = false;
        return;
    }
    
    struct pcap_pkthdr* header;
    const u_char* packet_data;
    auto start_time = std::chrono::steady_clock::now();
    uint64_t first_packet_ts = 0;
    
    while (!should_stop_ && reading_) {
        int result = pcap_next_ex(pcap_handle, &header, &packet_data);
        
        if (result == 1) { // Packet read successfully
            packets_processed_++;
            
            // Calculate replay timing for realistic playback
            uint64_t packet_ts = static_cast<uint64_t>(header->ts.tv_sec) * 1000000ULL + header->ts.tv_usec;
            
            if (first_packet_ts == 0) {
                first_packet_ts = packet_ts;
            }
            
            // Apply replay speed control
            if (replay_speed_ > 0.0 && replay_speed_ != 1.0) {
                uint64_t elapsed_packet_time = packet_ts - first_packet_ts;
                uint64_t target_real_time = static_cast<uint64_t>(elapsed_packet_time / replay_speed_);
                
                auto current_real_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                
                if (target_real_time > current_real_time) {
                    std::this_thread::sleep_for(std::chrono::microseconds(target_real_time - current_real_time));
                }
            }
            
            // Process the packet
            if (process_packet(packet_data, header->caplen, packet_ts * 1000ULL)) {
                packets_parsed_++;
            }
            
        } else if (result == -2) { // End of file
            if (loop_replay_) {
                logger_.info("Reached end of PCAP file, looping...");
                pcap_close(pcap_handle);
                pcap_handle = pcap_open_offline(pcap_file_.c_str(), errbuf);
                if (!pcap_handle) {
                    logger_.error("Failed to reopen PCAP file for looping: " + std::string(errbuf));
                    break;
                }
                first_packet_ts = 0;
                start_time = std::chrono::steady_clock::now();
                continue;
            } else {
                logger_.info("Reached end of PCAP file");
                break;
            }
        } else if (result == -1) { // Error reading packet
            logger_.error("Error reading PCAP packet: " + std::string(pcap_geterr(pcap_handle)));
            break;
        }
    }
    
    pcap_close(pcap_handle);
    reading_ = false;
    
    logger_.info("PCAP processing complete. Processed: " + std::to_string(packets_processed_.load()) +
                ", Parsed: " + std::to_string(packets_parsed_.load()) +
                ", Errors: " + std::to_string(parse_errors_.load()));
#endif // HAS_PCAP
}

bool PCAPReader::process_packet(const uint8_t* packet_data, size_t packet_len, uint64_t timestamp_ns) {
    if (packet_len < MIN_MARKET_DATA_PACKET_SIZE) {
        return false; // Too small to be a valid market data packet
    }
    
    // Extract UDP payload
    const uint8_t* payload;
    size_t payload_len;
    if (!extract_udp_payload(packet_data, packet_len, payload, payload_len)) {
        return false; // Not a UDP packet or extraction failed
    }
    
    // Parse based on feed format
    MarketDataPacket packet;
    packet.timestamp = std::chrono::nanoseconds(timestamp_ns);
    packet.format = feed_format_;
    
    bool parsed = false;
    switch (feed_format_) {
        case FeedFormat::NASDAQ_ITCH_5_0:
            parsed = parse_nasdaq_itch(payload, payload_len, packet);
            break;
        case FeedFormat::NYSE_PILLAR:
            parsed = parse_nyse_pillar(payload, payload_len, packet);
            break;
        case FeedFormat::IEX_TOPS:
            parsed = parse_iex_tops(payload, payload_len, packet);
            break;
        case FeedFormat::FIX_PROTOCOL:
            parsed = parse_fix_protocol(payload, payload_len, packet);
            break;
        case FeedFormat::GENERIC_CSV:
            parsed = parse_generic_csv(payload, payload_len, packet);
            break;
        default:
            logger_.warning("Unknown feed format: " + std::to_string(static_cast<int>(feed_format_)));
            return false;
    }
    
    if (parsed && data_callback_) {
        MarketData data = convert_to_market_data(packet);
        data_callback_(data);
        return true;
    }
    
    if (!parsed) {
        parse_errors_++;
    }
    
    return parsed;
}

bool PCAPReader::extract_udp_payload(const uint8_t* packet, size_t len, const uint8_t*& payload, size_t& payload_len) {
    if (len < ETH_HEADER_SIZE + IP_HEADER_SIZE + UDP_HEADER_SIZE) {
        return false;
    }
    
    // Skip Ethernet header (14 bytes)
    const uint8_t* ip_header = packet + ETH_HEADER_SIZE;
    
    // Check IP protocol (UDP = 17)
    if (ip_header[9] != 17) {
        return false; // Not UDP
    }
    
    // Extract IP header length
    uint8_t ip_header_len = (ip_header[0] & 0x0F) * 4;
    if (ip_header_len < 20) {
        return false; // Invalid IP header length
    }
    
    // Skip to UDP header
    const uint8_t* udp_header = ip_header + ip_header_len;
    if (udp_header + UDP_HEADER_SIZE > packet + len) {
        return false; // Packet too short
    }
    
    // Extract UDP length
    uint16_t udp_len = ntohs(*reinterpret_cast<const uint16_t*>(udp_header + 4));
    
    // Extract payload
    payload = udp_header + UDP_HEADER_SIZE;
    payload_len = udp_len - UDP_HEADER_SIZE;
    
    // Validate payload length
    if (payload + payload_len > packet + len) {
        return false; // Payload extends beyond packet
    }
    
    return true;
}

bool PCAPReader::parse_nasdaq_itch(const uint8_t* payload, size_t len, MarketDataPacket& packet) {
    if (len < sizeof(ITCHMessageHeader)) {
        return false;
    }
    
    const ITCHMessageHeader* header = reinterpret_cast<const ITCHMessageHeader*>(payload);
    uint16_t message_len = ntohs(header->length);
    
    if (len < message_len) {
        return false;
    }
    
    // Parse based on ITCH message type
    switch (header->message_type) {
        case 'A': // Add Order (no quotes)
        case 'F': // Add Order with MPID
            // These don't provide quote data, skip
            return false;
            
        case 'Q': { // Cross Trade (provides price info)
            if (message_len < 40) return false;
            
            const uint8_t* data = payload + sizeof(ITCHMessageHeader);
            
            // Extract symbol (8 bytes, right-padded with spaces)
            packet.symbol = parse_symbol(reinterpret_cast<const char*>(data + 11), 8);
            
            // Extract cross price (8 bytes, big endian)
            uint64_t price_raw = be64toh(*reinterpret_cast<const uint64_t*>(data + 19));
            packet.last_price = parse_price_field(price_raw);
            
            // Extract quantity (8 bytes, big endian)
            uint64_t qty_raw = be64toh(*reinterpret_cast<const uint64_t*>(data + 27));
            packet.last_size = static_cast<uint32_t>(qty_raw);
            
            return true;
        }
        
        default:
            // Other message types - could implement more as needed
            return false;
    }
}

bool PCAPReader::parse_nyse_pillar(const uint8_t* payload, size_t len, MarketDataPacket& packet) {
    // Simplified NYSE Pillar parsing - would need full specification for production
    if (len < 32) return false;
    
    // This is a placeholder implementation
    // Real NYSE Pillar has complex binary format
    logger_.warning("NYSE Pillar parsing not fully implemented");
    return false;
}

bool PCAPReader::parse_iex_tops(const uint8_t* payload, size_t len, MarketDataPacket& packet) {
    // Simplified IEX TOPS parsing
    if (len < 40) return false;
    
    // IEX TOPS has JSON-like messages, this is a simplified version
    logger_.warning("IEX TOPS parsing not fully implemented");
    return false;
}

bool PCAPReader::parse_fix_protocol(const uint8_t* payload, size_t len, MarketDataPacket& packet) {
    // FIX protocol parsing - look for market data snapshot messages
    std::string fix_message(reinterpret_cast<const char*>(payload), len);
    
    // Basic FIX parsing - look for key fields
    if (fix_message.find("35=W") != std::string::npos) { // Market Data Snapshot
        // Extract symbol (tag 55)
        size_t symbol_pos = fix_message.find("55=");
        if (symbol_pos == std::string::npos) return false;
        
        size_t symbol_start = symbol_pos + 3;
        size_t symbol_end = fix_message.find("\x01", symbol_start);
        if (symbol_end == std::string::npos) return false;
        
        packet.symbol = fix_message.substr(symbol_start, symbol_end - symbol_start);
        
        // Extract bid price (tag 270 with side 1)
        // Extract ask price (tag 270 with side 2)
        // This is simplified - real FIX parsing is more complex
        
        return true;
    }
    
    return false;
}

bool PCAPReader::parse_generic_csv(const uint8_t* payload, size_t len, MarketDataPacket& packet) {
    // Parse CSV format: symbol,timestamp,bid,ask,bid_size,ask_size,last,last_size
    std::string csv_line(reinterpret_cast<const char*>(payload), len);
    
    // Remove any newlines
    csv_line.erase(std::remove(csv_line.begin(), csv_line.end(), '\n'), csv_line.end());
    csv_line.erase(std::remove(csv_line.begin(), csv_line.end(), '\r'), csv_line.end());
    
    std::istringstream ss(csv_line);
    std::string field;
    std::vector<std::string> fields;
    
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    
    if (fields.size() < 8) {
        return false;
    }
    
    try {
        packet.symbol = fields[0];
        // Skip timestamp (fields[1]) - use packet timestamp
        packet.bid_price = std::stod(fields[2]);
        packet.ask_price = std::stod(fields[3]);
        packet.bid_size = static_cast<uint32_t>(std::stoul(fields[4]));
        packet.ask_size = static_cast<uint32_t>(std::stoul(fields[5]));
        packet.last_price = std::stod(fields[6]);
        packet.last_size = static_cast<uint32_t>(std::stoul(fields[7]));
        
        return true;
    } catch (const std::exception& e) {
        logger_.warning("Failed to parse CSV line: " + csv_line + " - " + e.what());
        return false;
    }
}

std::string PCAPReader::parse_symbol(const char* symbol_data, size_t max_len) {
    std::string symbol(symbol_data, max_len);
    
    // Remove trailing spaces/nulls
    size_t end = symbol.find_last_not_of(" \0");
    if (end != std::string::npos) {
        symbol = symbol.substr(0, end + 1);
    }
    
    return symbol;
}

double PCAPReader::parse_price_field(uint64_t price_int, uint32_t decimal_places) {
    return static_cast<double>(price_int) / std::pow(10.0, decimal_places);
}

MarketData PCAPReader::convert_to_market_data(const MarketDataPacket& packet) {
    MarketData data = MessageFactory::create_market_data(
        packet.symbol,
        packet.bid_price,
        packet.ask_price,
        packet.bid_size,
        packet.ask_size,
        packet.last_price,
        packet.last_size
    );
    
    // Override timestamp with packet timestamp
    data.header.timestamp = packet.timestamp;
    
    return data;
}

#ifdef DPDK_ENABLED
bool PCAPReader::initialize_dpdk_pcap() {
    logger_.info("Initializing DPDK PCAP reader");
    
    // Initialize DPDK EAL if not already done
    const char* argv[] = {"pcap_reader", "-c", "0x1", "-n", "1", "--no-huge", "--no-pci"};
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    if (rte_eal_init(argc, const_cast<char**>(argv)) < 0) {
        logger_.error("Failed to initialize DPDK EAL");
        return false;
    }
    
    // Open PCAP file with DPDK
    pcapng_reader_ = rte_pcapng_fdopen(pcap_file_.c_str(), "r");
    if (!pcapng_reader_) {
        logger_.error("Failed to open PCAP file with DPDK: " + pcap_file_);
        return false;
    }
    
    logger_.info("DPDK PCAP reader initialized successfully");
    return true;
}

void PCAPReader::process_dpdk_pcap() {
    // DPDK-based PCAP processing would go here
    // This is a placeholder for the DPDK implementation
    logger_.warning("DPDK PCAP processing not fully implemented");
}
#endif

} // namespace hft