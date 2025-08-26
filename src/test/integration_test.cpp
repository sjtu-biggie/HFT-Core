#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include <zmq.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <unordered_map>

using namespace hft;

class IntegrationTest {
public:
    IntegrationTest() 
        : running_(false)
        , market_data_received_(0)
        , trading_signals_received_(0)
        , order_executions_received_(0)
        , position_updates_received_(0)
        , logger_("IntegrationTest", StaticConfig::get_logger_endpoint()) {
    }
    
    bool initialize() {
        logger_.info("Initializing Integration Test");
        
        try {
            context_ = std::make_unique<zmq::context_t>(1);
            
            // Market data subscriber
            market_data_sub_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
            market_data_sub_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
            market_data_sub_->connect("tcp://localhost:5556");
            
            // Trading signals subscriber
            signals_sub_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
            signals_sub_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
            signals_sub_->connect("tcp://localhost:5558");
            
            // Order executions subscriber
            executions_sub_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
            executions_sub_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
            executions_sub_->connect("tcp://localhost:5557");
            
            // Position updates subscriber
            positions_sub_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
            positions_sub_->setsockopt(ZMQ_SUBSCRIBE, "", 0);
            positions_sub_->connect("tcp://localhost:5559");
            
            logger_.info("All subscribers initialized");
            
            // Give sockets time to connect
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            return true;
            
        } catch (const zmq::error_t& e) {
            logger_.error("Initialization failed: " + std::string(e.what()));
            return false;
        }
    }
    
    void run_test(int duration_seconds = 60) {
        logger_.info("Starting integration test for " + std::to_string(duration_seconds) + " seconds");
        
        running_.store(true);
        
        // Start monitoring threads
        std::vector<std::thread> threads;
        
        threads.emplace_back([this]() { monitor_market_data(); });
        threads.emplace_back([this]() { monitor_trading_signals(); });
        threads.emplace_back([this]() { monitor_executions(); });
        threads.emplace_back([this]() { monitor_positions(); });
        
        // Run for specified duration
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + std::chrono::seconds(duration_seconds);
        
        while (std::chrono::steady_clock::now() < end_time && running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            print_statistics();
        }
        
        // Stop monitoring
        running_.store(false);
        
        // Wait for threads to complete
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        // Final statistics and validation
        print_final_results();
        validate_results();
    }
    
    void stop() {
        running_.store(false);
        
        if (market_data_sub_) market_data_sub_->close();
        if (signals_sub_) signals_sub_->close();
        if (executions_sub_) executions_sub_->close();
        if (positions_sub_) positions_sub_->close();
    }

private:
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> market_data_sub_;
    std::unique_ptr<zmq::socket_t> signals_sub_;
    std::unique_ptr<zmq::socket_t> executions_sub_;
    std::unique_ptr<zmq::socket_t> positions_sub_;
    
    std::atomic<bool> running_;
    std::atomic<uint64_t> market_data_received_;
    std::atomic<uint64_t> trading_signals_received_;
    std::atomic<uint64_t> order_executions_received_;
    std::atomic<uint64_t> position_updates_received_;
    
    std::unordered_map<std::string, uint64_t> symbol_counts_;
    std::unordered_map<std::string, double> last_prices_;
    
    Logger logger_;
    
    void monitor_market_data() {
        logger_.info("Market data monitoring started");
        
        while (running_.load()) {
            try {
                zmq::message_t message;
                if (market_data_sub_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(MarketData)) {
                        MarketData data;
                        std::memcpy(&data, message.data(), sizeof(MarketData));
                        
                        std::string symbol(data.symbol);
                        symbol_counts_[symbol]++;
                        last_prices_[symbol] = data.last_price;
                        
                        market_data_received_++;
                        
                        // Validate data
                        if (!MessageFactory::validate_message(Message{.market_data = data})) {
                            logger_.error("Invalid market data received for " + symbol);
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    logger_.error("Market data monitoring error: " + std::string(e.what()));
                }
            }
        }
        
        logger_.info("Market data monitoring stopped");
    }
    
    void monitor_trading_signals() {
        logger_.info("Trading signals monitoring started");
        
        while (running_.load()) {
            try {
                zmq::message_t message;
                if (signals_sub_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(TradingSignal)) {
                        TradingSignal signal;
                        std::memcpy(&signal, message.data(), sizeof(TradingSignal));
                        
                        trading_signals_received_++;
                        
                        std::string symbol(signal.symbol);
                        std::string action = (signal.action == SignalAction::BUY) ? "BUY" : "SELL";
                        
                        logger_.info("Signal: " + action + " " + std::to_string(signal.quantity) + 
                                   " " + symbol + " @ " + std::to_string(signal.price));
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    logger_.error("Signals monitoring error: " + std::string(e.what()));
                }
            }
        }
        
        logger_.info("Trading signals monitoring stopped");
    }
    
    void monitor_executions() {
        logger_.info("Executions monitoring started");
        
        while (running_.load()) {
            try {
                zmq::message_t message;
                if (executions_sub_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(OrderExecution)) {
                        OrderExecution execution;
                        std::memcpy(&execution, message.data(), sizeof(OrderExecution));
                        
                        order_executions_received_++;
                        
                        std::string symbol(execution.symbol);
                        logger_.info("Execution: " + symbol + " " + 
                                   std::to_string(execution.fill_quantity) + 
                                   " @ " + std::to_string(execution.fill_price));
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    logger_.error("Executions monitoring error: " + std::string(e.what()));
                }
            }
        }
        
        logger_.info("Executions monitoring stopped");
    }
    
    void monitor_positions() {
        logger_.info("Positions monitoring started");
        
        while (running_.load()) {
            try {
                zmq::message_t message;
                if (positions_sub_->recv(message, zmq::recv_flags::dontwait)) {
                    if (message.size() == sizeof(PositionUpdate)) {
                        PositionUpdate position;
                        std::memcpy(&position, message.data(), sizeof(PositionUpdate));
                        
                        position_updates_received_++;
                        
                        std::string symbol(position.symbol);
                        logger_.info("Position: " + symbol + " qty=" + 
                                   std::to_string(position.position) + 
                                   " pnl=" + std::to_string(position.unrealized_pnl));
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {
                    logger_.error("Positions monitoring error: " + std::string(e.what()));
                }
            }
        }
        
        logger_.info("Positions monitoring stopped");
    }
    
    void print_statistics() {
        std::cout << "=== Integration Test Statistics ===" << std::endl;
        std::cout << "Market Data Messages: " << market_data_received_.load() << std::endl;
        std::cout << "Trading Signals: " << trading_signals_received_.load() << std::endl;
        std::cout << "Order Executions: " << order_executions_received_.load() << std::endl;
        std::cout << "Position Updates: " << position_updates_received_.load() << std::endl;
        std::cout << "Unique Symbols: " << symbol_counts_.size() << std::endl;
        std::cout << std::endl;
    }
    
    void print_final_results() {
        std::cout << "\\n=== FINAL INTEGRATION TEST RESULTS ===" << std::endl;
        std::cout << "Total Market Data Messages: " << market_data_received_.load() << std::endl;
        std::cout << "Total Trading Signals: " << trading_signals_received_.load() << std::endl;
        std::cout << "Total Order Executions: " << order_executions_received_.load() << std::endl;
        std::cout << "Total Position Updates: " << position_updates_received_.load() << std::endl;
        std::cout << "Total Unique Symbols: " << symbol_counts_.size() << std::endl;
        
        std::cout << "\\nSymbol Message Counts:" << std::endl;
        for (const auto& [symbol, count] : symbol_counts_) {
            std::cout << "  " << symbol << ": " << count << " messages";
            if (last_prices_.count(symbol)) {
                std::cout << " (last price: $" << last_prices_[symbol] << ")";
            }
            std::cout << std::endl;
        }
    }
    
    void validate_results() {
        std::cout << "\\n=== VALIDATION RESULTS ===" << std::endl;
        
        bool passed = true;
        
        // Check that we received market data
        if (market_data_received_.load() == 0) {
            std::cout << "❌ No market data received" << std::endl;
            passed = false;
        } else {
            std::cout << "✅ Market data received: " << market_data_received_.load() << " messages" << std::endl;
        }
        
        // Check message flow consistency
        uint64_t signals = trading_signals_received_.load();
        uint64_t executions = order_executions_received_.load();
        uint64_t positions = position_updates_received_.load();
        
        if (signals > 0) {
            std::cout << "✅ Trading signals generated: " << signals << std::endl;
            
            if (executions > 0) {
                std::cout << "✅ Order executions received: " << executions << std::endl;
                
                if (positions > 0) {
                    std::cout << "✅ Position updates received: " << positions << std::endl;
                } else {
                    std::cout << "⚠️  No position updates received (may be expected if services not running)" << std::endl;
                }
            } else {
                std::cout << "⚠️  No executions received (may be expected if Order Gateway not running)" << std::endl;
            }
        } else {
            std::cout << "⚠️  No trading signals received (may be expected if Strategy Engine not running)" << std::endl;
        }
        
        // Check symbol diversity
        if (symbol_counts_.size() >= 5) {
            std::cout << "✅ Good symbol diversity: " << symbol_counts_.size() << " symbols" << std::endl;
        } else if (symbol_counts_.size() > 0) {
            std::cout << "⚠️  Limited symbol diversity: " << symbol_counts_.size() << " symbols" << std::endl;
        } else {
            std::cout << "❌ No symbols received" << std::endl;
            passed = false;
        }
        
        if (passed) {
            std::cout << "\\n🎉 Integration test PASSED!" << std::endl;
        } else {
            std::cout << "\\n❌ Integration test FAILED!" << std::endl;
        }
    }
};

static std::unique_ptr<IntegrationTest> g_test;

void signal_handler(int signal) {
    std::cout << "\\nReceived signal " << signal << ", stopping test..." << std::endl;
    if (g_test) {
        g_test->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "HFT System Integration Test v1.0" << std::endl;
    std::cout << "=================================" << std::endl;
    
    int duration = (argc > 1) ? std::atoi(argv[1]) : 20;  // Default 20 seconds
    
    // Initialize configuration
    StaticConfig::load_from_file("config/hft_config.conf");
    
    // Initialize logging
    GlobalLogger::instance().init("IntegrationTest", StaticConfig::get_logger_endpoint());
    
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    try {
        g_test = std::make_unique<IntegrationTest>();
        
        if (!g_test->initialize()) {
            std::cerr << "Failed to initialize Integration Test" << std::endl;
            return 1;
        }
        
        std::cout << "Starting integration test (duration: " << duration << " seconds)" << std::endl;
        std::cout << "This test will monitor all message flows in the HFT system." << std::endl;
        std::cout << "Make sure the following services are running:" << std::endl;
        std::cout << "  - Market Data Handler (or Mock Data Generator)" << std::endl;
        std::cout << "  - Strategy Engine (optional)" << std::endl;
        std::cout << "  - Order Gateway (optional)" << std::endl;
        std::cout << "  - Position & Risk Service (optional)" << std::endl;
        std::cout << "\\nPress Ctrl+C to stop early.\\n" << std::endl;
        
        g_test->run_test(duration);
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}