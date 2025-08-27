#include "historical_data_player.h"
#include "data_downloader.h"
#include "../common/static_config.h"
#include "../common/logging.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>

void print_usage() {
    std::cout << "Usage: hft_backtesting [OPTIONS]\n"
              << "Options:\n"
              << "  --config <file>     Configuration file path (default: config/hft_config.conf)\n"
              << "  --data <file>       Historical data CSV file\n"
              << "  --symbol <symbol>   Symbol to backtest (default: AAPL)\n"
              << "  --speed <multiplier> Playback speed multiplier (default: 1.0, 0 = no delay)\n"
              << "  --start <timestamp>  Start timestamp (Unix milliseconds)\n"
              << "  --end <timestamp>    End timestamp (Unix milliseconds)\n"
              << "  --download          Download historical data first\n"
              << "  --source <source>   Data source for download (yahoo, alpaca, alphavantage, iex, polygon)\n"
              << "  --interval <interval> Time interval (1min, 5min, 15min, 30min, 1hour, 1day)\n"
              << "  --start-date <date>  Start date for download (YYYY-MM-DD)\n"
              << "  --end-date <date>    End date for download (YYYY-MM-DD)\n"
              << "  --output-dir <dir>   Output directory for downloaded data\n"
              << "  --help              Show this help message\n"
              << "\nExamples:\n"
              << "  # Basic backtesting with existing data\n"
              << "  ./hft_backtesting --data data/AAPL_1day_2023-01-01_to_2023-12-31.csv --speed 10.0\n"
              << "\n"
              << "  # Download data first, then backtest\n"
              << "  ./hft_backtesting --download --symbol AAPL --source yahoo --interval 1day \\\n"
              << "    --start-date 2023-01-01 --end-date 2023-12-31 --output-dir data\n"
              << "\n"
              << "  # Backtest specific time range at maximum speed\n"
              << "  ./hft_backtesting --data data/AAPL.csv --speed 0 \\\n"
              << "    --start 1672531200000 --end 1704067200000\n";
}

int main(int argc, char* argv[]) {
    std::string config_file = "config/hft_config.conf";
    std::string data_file;
    std::string symbol = "AAPL";
    double speed = 1.0;
    uint64_t start_time = 0;
    uint64_t end_time = 0;
    bool download_data = false;
    std::string data_source = "yahoo";
    std::string interval = "1day";
    std::string start_date;
    std::string end_date;
    std::string output_dir = "data";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--data" && i + 1 < argc) {
            data_file = argv[++i];
        } else if (arg == "--symbol" && i + 1 < argc) {
            symbol = argv[++i];
        } else if (arg == "--speed" && i + 1 < argc) {
            speed = std::stod(argv[++i]);
        } else if (arg == "--start" && i + 1 < argc) {
            start_time = std::stoull(argv[++i]);
        } else if (arg == "--end" && i + 1 < argc) {
            end_time = std::stoull(argv[++i]);
        } else if (arg == "--download") {
            download_data = true;
        } else if (arg == "--source" && i + 1 < argc) {
            data_source = argv[++i];
        } else if (arg == "--interval" && i + 1 < argc) {
            interval = argv[++i];
        } else if (arg == "--start-date" && i + 1 < argc) {
            start_date = argv[++i];
        } else if (arg == "--end-date" && i + 1 < argc) {
            end_date = argv[++i];
        } else if (arg == "--output-dir" && i + 1 < argc) {
            output_dir = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage();
            return 1;
        }
    }
    
    // Initialize configuration
    if (!hft::StaticConfig::load_from_file(config_file.c_str())) {
        std::cerr << "Warning: Could not load config file: " << config_file << std::endl;
        std::cerr << "Using default configuration" << std::endl;
    }
    
    // Initialize logging
    hft::Logger logger("BacktestingMain", hft::StaticConfig::get_logger_endpoint());
    logger.info("Starting HFT Backtesting Framework");
    
    // Download data if requested
    if (download_data) {
        logger.info("Downloading historical data");
        
        hft::DataDownloader downloader;
        if (!downloader.initialize()) {
            logger.error("Failed to initialize data downloader");
            return 1;
        }
        
        // Set up progress callback
        downloader.set_progress_callback([&logger](const std::string& sym, int current, int total) {
            if (current == 0) {
                logger.info("Starting download for " + sym);
            } else {
                logger.info("Completed download for " + sym + " (" + 
                           std::to_string(current) + "/" + std::to_string(total) + ")");
            }
        });
        
        // Convert string parameters to enums
        hft::DataSource source = hft::DataSource::YAHOO_FINANCE;
        if (data_source == "alpaca") source = hft::DataSource::ALPACA;
        else if (data_source == "alphavantage") source = hft::DataSource::ALPHA_VANTAGE;
        else if (data_source == "iex") source = hft::DataSource::IEX_CLOUD;
        else if (data_source == "polygon") source = hft::DataSource::POLYGON;
        
        hft::TimeInterval time_interval = hft::TimeInterval::DAY_1;
        if (interval == "1min") time_interval = hft::TimeInterval::MINUTE_1;
        else if (interval == "5min") time_interval = hft::TimeInterval::MINUTE_5;
        else if (interval == "15min") time_interval = hft::TimeInterval::MINUTE_15;
        else if (interval == "30min") time_interval = hft::TimeInterval::MINUTE_30;
        else if (interval == "1hour") time_interval = hft::TimeInterval::HOUR_1;
        else if (interval == "1week") time_interval = hft::TimeInterval::WEEK_1;
        else if (interval == "1month") time_interval = hft::TimeInterval::MONTH_1;
        
        // Create download request
        hft::DataRequest request;
        request.symbol = symbol;
        request.source = source;
        request.interval = time_interval;
        request.start_date = start_date;
        request.end_date = end_date;
        request.output_file = output_dir + "/" + symbol + "_" + interval + 
                             "_" + start_date + "_to_" + end_date + ".csv";
        
        if (!downloader.download_symbol_data(request)) {
            logger.error("Failed to download data for " + symbol);
            return 1;
        }
        
        // Use downloaded file for backtesting
        data_file = request.output_file;
        logger.info("Data downloaded to: " + data_file);
    }
    
    // Validate data file
    if (data_file.empty()) {
        logger.error("No data file specified. Use --data or --download option.");
        print_usage();
        return 1;
    }
    
    // Initialize Historical Data Player
    logger.info("Initializing Historical Data Player");
    hft::HistoricalDataPlayer player;
    
    if (!player.initialize()) {
        logger.error("Failed to initialize Historical Data Player");
        return 1;
    }
    
    // Load historical data
    logger.info("Loading historical data from: " + data_file);
    if (!player.load_data_file(data_file)) {
        logger.error("Failed to load data file: " + data_file);
        return 1;
    }
    
    // Configure playback parameters
    player.set_playback_speed(speed);
    
    if (start_time != 0 || end_time != 0) {
        player.set_time_range(start_time, end_time);
        logger.info("Set time range filter: " + std::to_string(start_time) + 
                   " to " + std::to_string(end_time));
    }
    
    // Set completion callback
    bool playback_completed = false;
    player.set_on_playback_complete([&playback_completed, &logger]() {
        logger.info("Historical data playback completed");
        playback_completed = true;
    });
    
    // Start playback
    logger.info("Starting historical data playback");
    logger.info("Total data points: " + std::to_string(player.get_total_data_points()));
    logger.info("Playback speed: " + std::to_string(speed) + "x");
    
    player.start();
    
    // Monitor playback progress
    auto start_monitor_time = std::chrono::steady_clock::now();
    uint64_t last_messages_sent = 0;
    
    while (player.is_running() && !playback_completed) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        uint64_t messages_sent = player.get_messages_sent();
        double progress = player.get_playback_progress() * 100.0;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_monitor_time).count();
        uint64_t rate = (messages_sent - last_messages_sent) / 5; // Messages per second
        
        std::ostringstream progress_stream;
        progress_stream << std::fixed << std::setprecision(1) << progress;
        
        logger.info("Progress: " + progress_stream.str() + "% " +
                   "Messages: " + std::to_string(messages_sent) + " " +
                   "Rate: " + std::to_string(rate) + " msg/s " +
                   "Elapsed: " + std::to_string(elapsed) + "s");
        
        last_messages_sent = messages_sent;
    }
    
    // Stop playback
    player.stop();
    
    // Final statistics
    auto final_time = std::chrono::steady_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        final_time - start_monitor_time).count();
    
    logger.info("Backtesting completed successfully");
    logger.info("Total messages sent: " + std::to_string(player.get_messages_sent()));
    logger.info("Total elapsed time: " + std::to_string(total_elapsed) + " seconds");
    
    if (total_elapsed > 0) {
        double avg_rate = static_cast<double>(player.get_messages_sent()) / total_elapsed;
        std::ostringstream rate_stream;
        rate_stream << std::fixed << std::setprecision(1) << avg_rate;
        logger.info("Average rate: " + rate_stream.str() + " messages/second");
    }
    
    return 0;
}