#pragma once

#include "../common/message_types.h"
#include "../common/static_config.h"
#include "../common/logging.h"
#include <zmq.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <functional>

namespace hft {

struct HistoricalDataPoint {
    uint64_t timestamp;  // Unix timestamp in milliseconds
    char symbol[16];
    double bid_price;
    double ask_price;
    double last_price;
    uint64_t bid_volume;
    uint64_t ask_volume;
    uint64_t last_volume;
    
    // Additional fields for backtesting
    double high_price;
    double low_price;
    double open_price;
    uint64_t total_volume;
};

class HistoricalDataPlayer {
public:
    HistoricalDataPlayer();
    ~HistoricalDataPlayer();
    
    // Configuration
    bool load_data_file(const std::string& file_path);
    void set_playback_speed(double speed_multiplier = 1.0); // 1.0 = real-time, 0 = no delay
    void set_time_range(uint64_t start_time, uint64_t end_time);
    
    // Control
    bool initialize();
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Statistics
    uint64_t get_messages_sent() const { return messages_sent_.load(); }
    uint64_t get_total_data_points() const { return historical_data_.size(); }
    double get_playback_progress() const;
    
    // Event callbacks for backtesting framework
    void set_on_playback_complete(std::function<void()> callback) {
        on_playback_complete_ = callback;
    }
    
private:
    // Configuration
    std::string data_file_path_;
    double playback_speed_;
    uint64_t start_time_;
    uint64_t end_time_;
    
    // Data storage
    std::vector<HistoricalDataPoint> historical_data_;
    size_t current_index_;
    
    // ZeroMQ
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> publisher_;
    
    // Threading
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> playback_thread_;
    
    // Statistics
    std::atomic<uint64_t> messages_sent_;
    std::chrono::high_resolution_clock::time_point playback_start_time_;
    std::chrono::high_resolution_clock::time_point simulation_start_time_;
    
    // Logging
    Logger logger_;
    
    // Callbacks
    std::function<void()> on_playback_complete_;
    
    // Private methods
    void playback_loop();
    bool load_csv_file(const std::string& file_path);
    void publish_market_data(const HistoricalDataPoint& data_point);
    void calculate_sleep_time(uint64_t data_timestamp);
    MarketData convert_to_market_data(const HistoricalDataPoint& data_point);
};

} // namespace hft