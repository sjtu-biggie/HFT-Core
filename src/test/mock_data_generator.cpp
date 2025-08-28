#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <random>
#include <signal.h>

using namespace hft;

class MockDataGenerator {
public:
    MockDataGenerator() 
        : running_(false)
        , messages_sent_(0)
        , logger_("MockDataGenerator", StaticConfig::get_logger_endpoint()) {
    }
    
    bool initialize() {
        logger_.info("Initializing Mock Data Generator");
        
        try {
            context_ = std::make_unique<zmq::context_t>(1);
            publisher_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
            
            // Set socket options
            int sndhwm = 1000;
            publisher_->setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
            int linger = 0;
            publisher_->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            
            // Bind to market data endpoint
            publisher_->bind("tcp://localhost:5556");
            logger_.info("Mock data publisher bound to tcp://localhost:5556");
            
            // Give sockets time to bind
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            return true;
            
        } catch (const zmq::error_t& e) {
            logger_.error("Initialization failed: " + std::string(e.what()));
            return false;
        }
    }
    
    void start(int duration_seconds = 30, int frequency_hz = 100) {
        if (running_.load()) {
            logger_.warning("Generator already running");
            return;
        }
        
        logger_.info("Starting mock data generation for " + std::to_string(duration_seconds) + 
                    " seconds at " + std::to_string(frequency_hz) + " Hz");
        
        running_.store(true);
        
        // Start generation thread
        generation_thread_ = std::make_unique<std::thread>([this, duration_seconds, frequency_hz]() {
            generate_data(duration_seconds, frequency_hz);
        });
        
        logger_.info("Mock data generation started");
    }
    
    void stop() {
        if (!running_.load()) return;
        
        logger_.info("Stopping mock data generator");
        running_.store(false);
        
        if (generation_thread_ && generation_thread_->joinable()) {
            generation_thread_->join();
        }
        
        if (publisher_) {
            publisher_->close();
        }
        
        logger_.info("Generated " + std::to_string(messages_sent_.load()) + " market data messages");
    }
    
    bool is_running() const {
        return running_.load();
    }

private:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> publisher_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> generation_thread_;
    std::atomic<uint64_t> messages_sent_;
    Logger logger_;
    
    void generate_data(int duration_seconds, int frequency_hz) {
        logger_.info("Data generation thread started");
        
        // Setup random number generation
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> price_dist(100.0, 500.0);
        std::uniform_int_distribution<> size_dist(100, 10000);
        std::uniform_real_distribution<> spread_dist(0.01, 0.20);
        
        // Test symbols
        std::vector<std::string> symbols = {
            "AAPL", "GOOGL", "MSFT", "TSLA", "AMZN", "NVDA", "META", "NFLX",
            "SPY", "QQQ", "IWM", "GLD", "TLT", "VIX", "TQQQ", "SQQQ"
        };
        
        std::uniform_int_distribution<> symbol_dist(0, symbols.size() - 1);
        
        // Track current prices for each symbol (random walk)
        std::unordered_map<std::string, double> current_prices;
        for (const auto& symbol : symbols) {
            current_prices[symbol] = price_dist(gen);
        }
        
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::seconds(duration_seconds);
        auto interval = std::chrono::microseconds(1000000 / frequency_hz);
        
        uint64_t message_count = 0;
        auto last_stats = start_time;
        
        while (running_.load() && std::chrono::steady_clock::now() < end_time) {
            auto loop_start = std::chrono::steady_clock::now();
            
            // Generate market data for random symbol
            std::string symbol = symbols[symbol_dist(gen)];
            
            // Random walk the price
            std::normal_distribution<> price_change_dist(0.0, 0.001); // 0.1% std dev
            double price_change = price_change_dist(gen);
            current_prices[symbol] *= (1.0 + price_change);
            
            double mid_price = current_prices[symbol];
            double spread = spread_dist(gen);
            double bid_price = mid_price - spread / 2.0;
            double ask_price = mid_price + spread / 2.0;
            
            uint32_t bid_size = size_dist(gen);
            uint32_t ask_size = size_dist(gen);
            
            // Generate last trade
            std::uniform_real_distribution<> trade_within_spread(0.0, 1.0);
            double last_price = bid_price + (ask_price - bid_price) * trade_within_spread(gen);
            uint32_t last_size = std::uniform_int_distribution<>(100, 1000)(gen);
            
            // Create and send market data
            MarketData data = MessageFactory::create_market_data(
                symbol, bid_price, ask_price, bid_size, ask_size, last_price, last_size
            );
            
            try {
                zmq::message_t message(sizeof(MarketData));
                std::memcpy(message.data(), &data, sizeof(MarketData));
                publisher_->send(message, zmq::send_flags::dontwait);
                
                messages_sent_++;
                message_count++;
                
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    logger_.error("Send failed: " + std::string(e.what()));
                }
            }
            
            // Log statistics every 10 seconds
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats >= std::chrono::seconds(10)) {
                double elapsed = std::chrono::duration<double>(now - start_time).count();
                double rate = message_count / elapsed;
                logger_.info("Generated " + std::to_string(message_count) + 
                           " messages (" + std::to_string(static_cast<int>(rate)) + " msg/sec)");
                last_stats = now;
            }
            
            // Sleep to maintain frequency
            auto loop_end = std::chrono::steady_clock::now();
            auto loop_duration = loop_end - loop_start;
            if (loop_duration < interval) {
                std::this_thread::sleep_for(interval - loop_duration);
            }
        }
        
        logger_.info("Data generation thread completed");
    }
};

static std::unique_ptr<MockDataGenerator> g_generator;

void signal_handler(int signal) {
    std::cout << "\\nReceived signal " << signal << ", shutting down..." << std::endl;
    if (g_generator) {
        g_generator->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT Mock Data Generator v1.0" << std::endl;
    std::cout << "============================" << std::endl;
    
    // Parse command line arguments
    int duration = (argc > 1) ? std::atoi(argv[1]) : 30;  // Default 30 seconds
    int frequency = (argc > 2) ? std::atoi(argv[2]) : 100;  // Default 100 Hz
    
    // Initialize logging
    GlobalLogger::instance().init("MockDataGenerator");
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_generator = std::make_unique<MockDataGenerator>();
        
        if (!g_generator->initialize()) {
            std::cerr << "Failed to initialize Mock Data Generator" << std::endl;
            return 1;
        }
        
        g_generator->start(duration, frequency);
        
        std::cout << "Mock Data Generator running for " << duration 
                  << " seconds at " << frequency << " Hz. Press Ctrl+C to stop early." << std::endl;
        
        while (g_generator->is_running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Mock Data Generator completed." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}