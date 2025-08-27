#include "data_downloader.h"
#include "../common/static_config.h"
#include <curl/curl.h>
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iomanip>
#include <unordered_set>
#include <random>
#include <ctime>

namespace hft {

// HTTP response callback for curl
size_t WriteDataCallback(void* contents, size_t size, size_t nmemb, std::string* data) {
    size_t totalSize = size * nmemb;
    data->append((char*)contents, totalSize);
    return totalSize;
}

DataDownloader::DataDownloader() 
    : logger_("DataDownloader", StaticConfig::get_logger_endpoint()) {
    initialize_source_configs();
}

DataDownloader::~DataDownloader() {
    curl_global_cleanup();
}

bool DataDownloader::initialize() {
    logger_.info("Initializing Data Downloader");
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Initialize clients based on available credentials
    const char* alpaca_key = std::getenv("ALPACA_API_KEY");
    const char* alpaca_secret = std::getenv("ALPACA_API_SECRET");
    if (alpaca_key && alpaca_secret) {
        alpaca_client_ = std::make_unique<AlpacaDataClient>(alpaca_key, alpaca_secret);
        logger_.info("Alpaca data client initialized");
    }
    
    const char* av_key = std::getenv("ALPHA_VANTAGE_API_KEY");
    if (av_key) {
        alphavantage_client_ = std::make_unique<AlphaVantageClient>(av_key);
        logger_.info("Alpha Vantage client initialized");
    }
    
    const char* iex_token = std::getenv("IEX_CLOUD_API_TOKEN");
    if (iex_token) {
        iex_client_ = std::make_unique<IEXCloudClient>(iex_token);
        logger_.info("IEX Cloud client initialized");
    }
    
    const char* polygon_key = std::getenv("POLYGON_API_KEY");
    if (polygon_key) {
        polygon_client_ = std::make_unique<PolygonClient>(polygon_key);
        logger_.info("Polygon client initialized");
    }
    
    // Yahoo Finance doesn't require API key
    yahoo_client_ = std::make_unique<YahooFinanceClient>();
    logger_.info("Yahoo Finance client initialized");
    
    logger_.info("Data Downloader initialized successfully");
    return true;
}

bool DataDownloader::download_symbol_data(const DataRequest& request) {
    logger_.info("Downloading data for " + request.symbol + " from " + 
                source_to_string(request.source));
    
    if (progress_callback_) {
        progress_callback_(request.symbol, 0, 1);
    }
    
    // Respect rate limits
    respect_rate_limits(request.source);
    
    bool result = false;
    switch (request.source) {
        case DataSource::ALPACA:
            result = download_from_alpaca(request);
            break;
        case DataSource::YAHOO_FINANCE:
            result = download_from_yahoo(request);
            break;
        case DataSource::ALPHA_VANTAGE:
            result = download_from_alphavantage(request);
            break;
        case DataSource::IEX_CLOUD:
            result = download_from_iex(request);
            break;
        case DataSource::POLYGON:
            result = download_from_polygon(request);
            break;
        case DataSource::CSV_FILE:
            result = load_from_csv(request);
            break;
        default:
            logger_.error("Unsupported data source: " + source_to_string(request.source));
            result = false;
    }
    
    if (progress_callback_) {
        progress_callback_(request.symbol, 1, 1);
    }
    
    if (result) {
        logger_.info("Successfully downloaded data for " + request.symbol);
    } else {
        logger_.error("Failed to download data for " + request.symbol);
    }
    
    return result;
}

bool DataDownloader::download_multiple_symbols(const std::vector<DataRequest>& requests) {
    logger_.info("Downloading data for " + std::to_string(requests.size()) + " symbols");
    
    int completed = 0;
    int total = requests.size();
    bool all_successful = true;
    
    for (const auto& request : requests) {
        if (progress_callback_) {
            progress_callback_(request.symbol, completed, total);
        }
        
        bool result = download_symbol_data(request);
        all_successful = all_successful && result;
        completed++;
        
        // Small delay between requests to be respectful to APIs
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    logger_.info("Completed downloading " + std::to_string(completed) + " symbols");
    return all_successful;
}

bool DataDownloader::download_symbol_list(const std::vector<std::string>& symbols,
                                        DataSource source,
                                        TimeInterval interval,
                                        const std::string& start_date,
                                        const std::string& end_date,
                                        const std::string& output_dir) {
    
    std::vector<DataRequest> requests;
    for (const auto& symbol : symbols) {
        DataRequest request;
        request.symbol = symbol;
        request.source = source;
        request.interval = interval;
        request.start_date = start_date;
        request.end_date = end_date;
        request.output_file = output_dir + "/" + symbol + "_" + 
                             interval_to_string(interval) + "_" +
                             start_date + "_to_" + end_date + ".csv";
        requests.push_back(request);
    }
    
    return download_multiple_symbols(requests);
}

ValidationResult DataDownloader::validate_data_file(const std::string& file_path) {
    ValidationResult result{};
    result.valid = false;
    
    try {
        std::vector<HistoricalDataPoint> data = read_data_from_csv(file_path);
        
        if (data.empty()) {
            result.error_message = "No data found in file";
            return result;
        }
        
        result.total_points = data.size();
        
        // Check for duplicates
        std::unordered_set<uint64_t> timestamps;
        for (const auto& point : data) {
            if (timestamps.find(point.timestamp) != timestamps.end()) {
                result.duplicate_points++;
            } else {
                timestamps.insert(point.timestamp);
            }
        }
        
        // Check time range
        auto min_time = std::min_element(data.begin(), data.end(),
            [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        auto max_time = std::max_element(data.begin(), data.end(),
            [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        
        result.time_range = timestamp_to_date_string(min_time->timestamp) + " to " +
                           timestamp_to_date_string(max_time->timestamp);
        
        // Basic data consistency checks
        result.valid = validate_data_consistency(data);
        
        if (!result.valid && result.error_message.empty()) {
            result.error_message = "Data consistency checks failed";
        }
        
    } catch (const std::exception& e) {
        result.error_message = "Exception validating file: " + std::string(e.what());
    }
    
    return result;
}

bool DataDownloader::download_from_yahoo(const DataRequest& request) {
    if (!yahoo_client_) {
        logger_.error("Yahoo Finance client not initialized");
        return false;
    }
    
    try {
        std::string interval_str;
        switch (request.interval) {
            case TimeInterval::DAY_1: interval_str = "1d"; break;
            case TimeInterval::WEEK_1: interval_str = "1wk"; break;
            case TimeInterval::MONTH_1: interval_str = "1mo"; break;
            default:
                logger_.error("Unsupported interval for Yahoo Finance");
                return false;
        }
        
        auto data = yahoo_client_->get_historical_data(request.symbol, 
                                                      request.start_date,
                                                      request.end_date, 
                                                      interval_str);
        
        if (data.empty()) {
            logger_.error("No data received from Yahoo Finance");
            return false;
        }
        
        if (!request.output_file.empty()) {
            return write_data_to_csv(data, request.output_file);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("Yahoo Finance download failed: " + std::string(e.what()));
        return false;
    }
}

std::vector<HistoricalDataPoint> DataDownloader::read_data_from_csv(const std::string& file_path) {
    std::vector<HistoricalDataPoint> data;
    std::ifstream file(file_path);
    
    if (!file.is_open()) {
        logger_.error("Failed to open file: " + file_path);
        return data;
    }
    
    std::string line;
    bool first_line = true;
    
    while (std::getline(file, line)) {
        if (first_line) {
            first_line = false;
            continue; // Skip header
        }
        
        std::stringstream ss(line);
        std::string cell;
        HistoricalDataPoint point{};
        
        try {
            int field = 0;
            while (std::getline(ss, cell, ',') && field < 9) {
                switch (field) {
                    case 0: point.timestamp = std::stoull(cell); break;
                    case 1: strncpy(point.symbol, cell.c_str(), sizeof(point.symbol) - 1); break;
                    case 2: point.open_price = std::stod(cell); break;
                    case 3: point.high_price = std::stod(cell); break;
                    case 4: point.low_price = std::stod(cell); break;
                    case 5: point.last_price = std::stod(cell); break;
                    case 6: point.total_volume = std::stoull(cell); break;
                    case 7: point.bid_price = std::stod(cell); break;
                    case 8: point.ask_price = std::stod(cell); break;
                }
                field++;
            }
            data.push_back(point);
        } catch (const std::exception& e) {
            logger_.warning("Skipping invalid line in CSV: " + line);
        }
    }
    
    return data;
}

bool DataDownloader::write_data_to_csv(const std::vector<HistoricalDataPoint>& data,
                                      const std::string& file_path) {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        logger_.error("Failed to create output file: " + file_path);
        return false;
    }
    
    // Write header
    file << "timestamp,symbol,open,high,low,close,volume,bid,ask\n";
    
    // Write data
    for (const auto& point : data) {
        file << point.timestamp << ","
             << point.symbol << ","
             << std::fixed << std::setprecision(4)
             << point.open_price << ","
             << point.high_price << ","
             << point.low_price << ","
             << point.last_price << ","
             << point.total_volume << ","
             << point.bid_price << ","
             << point.ask_price << "\n";
    }
    
    file.close();
    logger_.info("Wrote " + std::to_string(data.size()) + " data points to " + file_path);
    return true;
}

bool DataDownloader::validate_data_consistency(const std::vector<HistoricalDataPoint>& data) {
    for (const auto& point : data) {
        // Basic price validation
        if (point.high_price < point.low_price) {
            logger_.error("Invalid price data: high < low for " + std::string(point.symbol));
            return false;
        }
        
        if (point.open_price < 0 || point.last_price < 0) {
            logger_.error("Negative prices found for " + std::string(point.symbol));
            return false;
        }
        
        // Volume validation
        if (point.total_volume == 0) {
            logger_.warning("Zero volume found for " + std::string(point.symbol));
        }
    }
    
    return true;
}

void DataDownloader::respect_rate_limits(DataSource source) {
    auto& config = source_configs_[source];
    auto& last_request = last_request_times_[source];
    auto& request_count = request_counts_[source];
    
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    // Reset request count if a minute has passed
    if (now - last_request > 60000) { // 60 seconds
        request_count = 0;
    }
    
    // Check if we need to wait
    if (request_count >= config.rate_limit_requests_per_minute) {
        uint64_t wait_time = 60000 - (now - last_request);
        if (wait_time > 0) {
            logger_.info("Rate limiting: waiting " + std::to_string(wait_time) + "ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_time));
        }
        request_count = 0;
    }
    
    last_request_times_[source] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    request_counts_[source]++;
}

void DataDownloader::initialize_source_configs() {
    // Alpaca configuration
    source_configs_[DataSource::ALPACA] = {
        "https://data.alpaca.markets",
        200,  // 200 requests per minute
        10000, // 10k requests per day
        {},
        true
    };
    
    // Yahoo Finance configuration
    source_configs_[DataSource::YAHOO_FINANCE] = {
        "https://query1.finance.yahoo.com",
        60,   // Conservative rate limit
        2000, // Conservative daily limit
        {},
        false
    };
    
    // Alpha Vantage configuration
    source_configs_[DataSource::ALPHA_VANTAGE] = {
        "https://www.alphavantage.co",
        5,    // 5 requests per minute (free tier)
        500,  // 500 requests per day (free tier)
        {},
        true
    };
    
    // IEX Cloud configuration
    source_configs_[DataSource::IEX_CLOUD] = {
        "https://cloud.iexapis.com",
        100,  // 100 requests per second (generous limit)
        1000000, // 1M requests per month
        {},
        true
    };
    
    // Polygon configuration
    source_configs_[DataSource::POLYGON] = {
        "https://api.polygon.io",
        60,   // Conservative rate limit
        50000, // Depends on subscription
        {},
        true
    };
}

// Utility functions
std::string DataDownloader::interval_to_string(TimeInterval interval) {
    switch (interval) {
        case TimeInterval::MINUTE_1: return "1min";
        case TimeInterval::MINUTE_5: return "5min";
        case TimeInterval::MINUTE_15: return "15min";
        case TimeInterval::MINUTE_30: return "30min";
        case TimeInterval::HOUR_1: return "1hour";
        case TimeInterval::DAY_1: return "1day";
        case TimeInterval::WEEK_1: return "1week";
        case TimeInterval::MONTH_1: return "1month";
        default: return "unknown";
    }
}

std::string DataDownloader::source_to_string(DataSource source) {
    switch (source) {
        case DataSource::ALPACA: return "Alpaca";
        case DataSource::YAHOO_FINANCE: return "Yahoo Finance";
        case DataSource::ALPHA_VANTAGE: return "Alpha Vantage";
        case DataSource::IEX_CLOUD: return "IEX Cloud";
        case DataSource::POLYGON: return "Polygon";
        case DataSource::CSV_FILE: return "CSV File";
        default: return "Unknown";
    }
}

bool DataDownloader::is_source_available(DataSource source) const {
    switch (source) {
        case DataSource::ALPACA: return alpaca_client_ != nullptr;
        case DataSource::YAHOO_FINANCE: return yahoo_client_ != nullptr;
        case DataSource::ALPHA_VANTAGE: return alphavantage_client_ != nullptr;
        case DataSource::IEX_CLOUD: return iex_client_ != nullptr;
        case DataSource::POLYGON: return polygon_client_ != nullptr;
        case DataSource::CSV_FILE: return true; // Always available
        default: return false;
    }
}

uint64_t DataDownloader::parse_date_string(const std::string& date_str) {
    std::tm tm = {};
    std::istringstream ss(date_str);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    return std::mktime(&tm) * 1000; // Convert to milliseconds
}

std::string DataDownloader::timestamp_to_date_string(uint64_t timestamp) {
    auto time_t_val = static_cast<time_t>(timestamp / 1000);
    std::tm* tm = std::gmtime(&time_t_val);
    
    std::stringstream ss;
    ss << std::put_time(tm, "%Y-%m-%d");
    return ss.str();
}

bool DataDownloader::is_market_day(uint64_t timestamp) {
    auto time_t_val = static_cast<time_t>(timestamp / 1000);
    std::tm* tm = std::gmtime(&time_t_val);
    
    // Simple implementation: Monday-Friday (weekday 1-5)
    int weekday = tm->tm_wday;
    return weekday >= 1 && weekday <= 5;
}

int DataDownloader::get_interval_minutes(TimeInterval interval) {
    switch (interval) {
        case TimeInterval::MINUTE_1: return 1;
        case TimeInterval::MINUTE_5: return 5;
        case TimeInterval::MINUTE_15: return 15;
        case TimeInterval::MINUTE_30: return 30;
        case TimeInterval::HOUR_1: return 60;
        case TimeInterval::DAY_1: return 24 * 60;
        case TimeInterval::WEEK_1: return 7 * 24 * 60;
        case TimeInterval::MONTH_1: return 30 * 24 * 60;
        default: return 24 * 60;
    }
}

// Stub implementations for data source methods
bool DataDownloader::download_from_alpaca(const DataRequest&) {
    logger_.warning("Alpaca download not implemented");
    return false;
}

bool DataDownloader::download_from_alphavantage(const DataRequest&) {
    logger_.warning("Alpha Vantage download not implemented");
    return false;
}

bool DataDownloader::download_from_iex(const DataRequest&) {
    logger_.warning("IEX Cloud download not implemented");
    return false;
}

bool DataDownloader::download_from_polygon(const DataRequest&) {
    logger_.warning("Polygon download not implemented");
    return false;
}

bool DataDownloader::load_from_csv(const DataRequest& request) {
    logger_.info("Loading data from CSV file: " + request.output_file);
    
    if (request.output_file.empty()) {
        logger_.error("No CSV file path specified");
        return false;
    }
    
    std::vector<HistoricalDataPoint> data = read_data_from_csv(request.output_file);
    
    if (data.empty()) {
        logger_.error("No data loaded from CSV file");
        return false;
    }
    
    logger_.info("Successfully loaded " + std::to_string(data.size()) + " data points from CSV");
    return true;
}

TimeInterval DataDownloader::string_to_interval(const std::string& interval_str) {
    if (interval_str == "1min") return TimeInterval::MINUTE_1;
    if (interval_str == "5min") return TimeInterval::MINUTE_5;
    if (interval_str == "15min") return TimeInterval::MINUTE_15;
    if (interval_str == "30min") return TimeInterval::MINUTE_30;
    if (interval_str == "1hour") return TimeInterval::HOUR_1;
    if (interval_str == "1day") return TimeInterval::DAY_1;
    if (interval_str == "1week") return TimeInterval::WEEK_1;
    if (interval_str == "1month") return TimeInterval::MONTH_1;
    return TimeInterval::DAY_1; // default
}

DataSource DataDownloader::string_to_source(const std::string& source_str) {
    if (source_str == "Alpaca") return DataSource::ALPACA;
    if (source_str == "Yahoo Finance") return DataSource::YAHOO_FINANCE;
    if (source_str == "Alpha Vantage") return DataSource::ALPHA_VANTAGE;
    if (source_str == "IEX Cloud") return DataSource::IEX_CLOUD;
    if (source_str == "Polygon") return DataSource::POLYGON;
    if (source_str == "CSV File") return DataSource::CSV_FILE;
    return DataSource::YAHOO_FINANCE; // default
}

std::vector<std::string> DataDownloader::get_supported_symbols(DataSource source) const {
    // Return empty vector for stub implementation
    // In a real implementation, this would query the data source for available symbols
    return {};
}

bool DataDownloader::merge_data_files(const std::vector<std::string>& input_files,
                                     const std::string& output_file) {
    logger_.info("Merging " + std::to_string(input_files.size()) + " data files");
    
    std::vector<HistoricalDataPoint> all_data;
    
    for (const auto& file : input_files) {
        auto file_data = read_data_from_csv(file);
        all_data.insert(all_data.end(), file_data.begin(), file_data.end());
    }
    
    // Sort by timestamp
    std::sort(all_data.begin(), all_data.end(),
        [](const HistoricalDataPoint& a, const HistoricalDataPoint& b) {
            return a.timestamp < b.timestamp;
        });
    
    // Remove duplicates
    remove_duplicates(all_data);
    
    return write_data_to_csv(all_data, output_file);
}

bool DataDownloader::convert_data_format(const std::string& input_file,
                                        const std::string& output_file,
                                        const std::string& input_format,
                                        const std::string& output_format) {
    logger_.warning("Data format conversion not implemented");
    return false;
}

void DataDownloader::remove_duplicates(std::vector<HistoricalDataPoint>& data) {
    std::sort(data.begin(), data.end(),
        [](const HistoricalDataPoint& a, const HistoricalDataPoint& b) {
            if (a.timestamp != b.timestamp) return a.timestamp < b.timestamp;
            return strcmp(a.symbol, b.symbol) < 0;
        });
    
    auto it = std::unique(data.begin(), data.end(),
        [](const HistoricalDataPoint& a, const HistoricalDataPoint& b) {
            return a.timestamp == b.timestamp && strcmp(a.symbol, b.symbol) == 0;
        });
    
    data.erase(it, data.end());
}

void DataDownloader::fill_missing_data(std::vector<HistoricalDataPoint>& data, TimeInterval interval) {
    logger_.warning("Missing data filling not implemented");
}

std::string DataDownloader::make_http_request(const std::string& url, 
                                            const std::vector<std::string>& headers) {
    logger_.warning("HTTP request functionality not implemented");
    return "";
}

std::vector<HistoricalDataPoint> DataDownloader::parse_json_response(const std::string& response,
                                                                    DataSource source,
                                                                    const std::string& symbol) {
    logger_.warning("JSON parsing not implemented");
    return {};
}

} // namespace hft
