#include "historical_data_player.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace hft {

HistoricalDataPlayer::HistoricalDataPlayer()
    : playback_speed_(1.0)
    , start_time_(0)
    , end_time_(0)
    , current_index_(0)
    , running_(false)
    , messages_sent_(0)
    , logger_("HistoricalDataPlayer", StaticConfig::get_logger_endpoint()) {
}

HistoricalDataPlayer::~HistoricalDataPlayer() {
    stop();
}

bool HistoricalDataPlayer::initialize() {
    logger_.info("Initializing Historical Data Player");
    
    try {
        // Initialize ZeroMQ context and publisher
        context_ = std::make_unique<zmq::context_t>(1);
        publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        
        // Set socket options for high performance
        int sndhwm = 1000;
        publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        int linger = 0;
        publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        
        // Bind to the same endpoint as market data handler
        const char* endpoint = StaticConfig::get_market_data_endpoint();
        publisher_->bind(endpoint);
        
        logger_.info("Historical Data Player bound to " + std::string(endpoint));
        
        if (historical_data_.empty()) {
            logger_.warning("No historical data loaded. Use load_data_file() first.");
            return false;
        }
        
        logger_.info("Loaded " + std::to_string(historical_data_.size()) + " historical data points");
        return true;
        
    } catch (const zmq::error_t& e) {
        logger_.error("ZeroMQ initialization failed: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logger_.error("Initialization failed: " + std::string(e.what()));
        return false;
    }
}

bool HistoricalDataPlayer::load_data_file(const std::string& file_path) {
    data_file_path_ = file_path;
    logger_.info("Loading historical data from: " + file_path);
    
    if (!load_csv_file(file_path)) {
        logger_.error("Failed to load data file: " + file_path);
        return false;
    }
    
    // Sort data by timestamp to ensure chronological order
    std::sort(historical_data_.begin(), historical_data_.end(),
              [](const HistoricalDataPoint& a, const HistoricalDataPoint& b) {
                  return a.timestamp < b.timestamp;
              });
    
    logger_.info("Loaded " + std::to_string(historical_data_.size()) + " data points");
    
    if (!historical_data_.empty()) {
        logger_.info("Time range: " + std::to_string(historical_data_.front().timestamp) + 
                     " to " + std::to_string(historical_data_.back().timestamp));
    }
    
    return true;
}

bool HistoricalDataPlayer::load_csv_file(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    bool is_first_line = true;
    
    while (std::getline(file, line)) {
        // Skip header line
        if (is_first_line) {
            is_first_line = false;
            continue;
        }
        
        std::stringstream ss(line);
        std::string cell;
        HistoricalDataPoint data_point = {};
        
        try {
            // Expected CSV format: timestamp,symbol,open,high,low,close,volume,bid,ask
            int field = 0;
            while (std::getline(ss, cell, ',') && field < 9) {
                switch (field) {
                    case 0: // timestamp (Unix timestamp in milliseconds)
                        data_point.timestamp = std::stoull(cell);
                        break;
                    case 1: // symbol
                        strncpy(data_point.symbol, cell.c_str(), sizeof(data_point.symbol) - 1);
                        data_point.symbol[sizeof(data_point.symbol) - 1] = '\0';
                        break;
                    case 2: // open
                        data_point.open_price = std::stod(cell);
                        break;
                    case 3: // high
                        data_point.high_price = std::stod(cell);
                        break;
                    case 4: // low
                        data_point.low_price = std::stod(cell);
                        break;
                    case 5: // close (use as last price)
                        data_point.last_price = std::stod(cell);
                        break;
                    case 6: // volume
                        data_point.total_volume = std::stoull(cell);
                        data_point.last_volume = data_point.total_volume; // Simplification
                        break;
                    case 7: // bid (if available, otherwise use close - spread/2)
                        if (!cell.empty() && cell != "null") {
                            data_point.bid_price = std::stod(cell);
                        } else {
                            data_point.bid_price = data_point.last_price * 0.999; // 0.1% spread
                        }
                        break;
                    case 8: // ask (if available, otherwise use close + spread/2)
                        if (!cell.empty() && cell != "null") {
                            data_point.ask_price = std::stod(cell);
                        } else {
                            data_point.ask_price = data_point.last_price * 1.001; // 0.1% spread
                        }
                        break;
                }
                field++;
            }
            
            // Set default volumes if not provided
            if (data_point.bid_volume == 0) data_point.bid_volume = 1000;
            if (data_point.ask_volume == 0) data_point.ask_volume = 1000;
            
            historical_data_.push_back(data_point);
            
        } catch (const std::exception& e) {
            logger_.warning("Skipping invalid line: " + line + " (" + e.what() + ")");
            continue;
        }
    }
    
    return !historical_data_.empty();
}

void HistoricalDataPlayer::set_playback_speed(double speed_multiplier) {
    playback_speed_ = speed_multiplier;
    logger_.info("Playback speed set to " + std::to_string(speed_multiplier) + "x");
}

void HistoricalDataPlayer::set_time_range(uint64_t start_time, uint64_t end_time) {
    start_time_ = start_time;
    end_time_ = end_time;
    logger_.info("Time range filter set: " + std::to_string(start_time) + " to " + std::to_string(end_time));
}

void HistoricalDataPlayer::start() {
    if (running_.load()) {
        logger_.warning("Historical Data Player is already running");
        return;
    }
    
    logger_.info("Starting Historical Data Player");
    running_.store(true);
    current_index_ = 0;
    messages_sent_.store(0);
    
    playback_start_time_ = std::chrono::high_resolution_clock::now();
    
    if (!historical_data_.empty()) {
        simulation_start_time_ = std::chrono::high_resolution_clock::time_point(
            std::chrono::milliseconds(historical_data_[0].timestamp));
    }
    
    // Start playback thread
    playback_thread_ = std::make_unique<std::thread>(&HistoricalDataPlayer::playback_loop, this);
    
    logger_.info("Historical Data Player started");
}

void HistoricalDataPlayer::stop() {
    if (!running_.load()) {
        return;
    }
    
    logger_.info("Stopping Historical Data Player");
    running_.store(false);
    
    if (playback_thread_ && playback_thread_->joinable()) {
        playback_thread_->join();
    }
    
    // Close sockets
    if (publisher_) publisher_->close();
    
    logger_.info("Historical Data Player stopped. Sent " + std::to_string(messages_sent_.load()) + " messages");
}

void HistoricalDataPlayer::playback_loop() {
    logger_.info("Historical data playback started");
    
    auto last_timestamp = std::chrono::high_resolution_clock::time_point();
    
    while (running_.load() && current_index_ < historical_data_.size()) {
        const auto& data_point = historical_data_[current_index_];
        
        // Apply time range filter if set
        if ((start_time_ != 0 && data_point.timestamp < start_time_) ||
            (end_time_ != 0 && data_point.timestamp > end_time_)) {
            current_index_++;
            continue;
        }
        
        // Calculate timing for realistic playback
        if (playback_speed_ > 0.0 && current_index_ > 0) {
            calculate_sleep_time(data_point.timestamp);
        }
        
        // Publish the market data
        publish_market_data(data_point);
        
        current_index_++;
        messages_sent_++;
        
        // Log progress periodically
        if (messages_sent_.load() % 1000 == 0) {
            logger_.info("Sent " + std::to_string(messages_sent_.load()) + " messages, progress: " +
                        std::to_string(get_playback_progress() * 100.0) + "%");
        }
    }
    
    logger_.info("Historical data playback completed. Total messages: " + std::to_string(messages_sent_.load()));
    
    // Notify completion
    if (on_playback_complete_) {
        on_playback_complete_();
    }
}

void HistoricalDataPlayer::calculate_sleep_time(uint64_t data_timestamp) {
    if (current_index_ == 0) return;
    
    // Calculate time difference between data points
    uint64_t time_diff = data_timestamp - historical_data_[current_index_ - 1].timestamp;
    
    // Scale by playback speed
    auto sleep_duration = std::chrono::milliseconds(static_cast<uint64_t>(time_diff / playback_speed_));
    
    if (sleep_duration.count() > 0) {
        std::this_thread::sleep_for(sleep_duration);
    }
}

void HistoricalDataPlayer::publish_market_data(const HistoricalDataPoint& data_point) {
    try {
        MarketData market_data = convert_to_market_data(data_point);
        
        zmq::message_t message(sizeof(MarketData));
        std::memcpy(message.data(), &market_data, sizeof(MarketData));
        
        publisher_->send(message, zmq::send_flags::dontwait);
        
    } catch (const zmq::error_t& e) {
        logger_.error("Failed to send market data: " + std::string(e.what()));
    }
}

MarketData HistoricalDataPlayer::convert_to_market_data(const HistoricalDataPoint& data_point) {
    MarketData market_data = {};
    
    // Create message header
    market_data.header = MessageFactory::create_header(MessageType::MARKET_DATA, sizeof(MarketData) - sizeof(MessageHeader));
    market_data.header.timestamp = timestamp_t(std::chrono::nanoseconds(data_point.timestamp * 1000000));
    
    // Copy symbol
    std::strncpy(market_data.symbol, data_point.symbol, sizeof(market_data.symbol) - 1);
    market_data.symbol[sizeof(market_data.symbol) - 1] = '\0';
    
    // Set prices and volumes
    market_data.bid_price = data_point.bid_price;
    market_data.ask_price = data_point.ask_price;
    market_data.last_price = data_point.last_price;
    market_data.bid_size = static_cast<uint32_t>(data_point.total_volume / 2);
    market_data.ask_size = static_cast<uint32_t>(data_point.total_volume / 2);
    market_data.last_size = static_cast<uint32_t>(data_point.total_volume);
    market_data.exchange_timestamp = data_point.timestamp * 1000000;
    
    return market_data;
}

double HistoricalDataPlayer::get_playback_progress() const {
    if (historical_data_.empty()) return 0.0;
    return static_cast<double>(current_index_) / historical_data_.size();
}

} // namespace hft