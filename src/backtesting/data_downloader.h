#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include "historical_data_player.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace hft {

// Data source types
enum class DataSource {
    ALPACA,         // Alpaca Markets API
    YAHOO_FINANCE,  // Yahoo Finance (free)
    ALPHA_VANTAGE,  // Alpha Vantage API
    IEX_CLOUD,      // IEX Cloud API
    CSV_FILE,       // Local CSV file
    POLYGON         // Polygon.io API
};

// Time interval for historical data
enum class TimeInterval {
    MINUTE_1,
    MINUTE_5,
    MINUTE_15,
    MINUTE_30,
    HOUR_1,
    DAY_1,
    WEEK_1,
    MONTH_1
};

// Data download request
struct DataRequest {
    std::string symbol;
    DataSource source;
    TimeInterval interval;
    std::string start_date;  // Format: YYYY-MM-DD
    std::string end_date;    // Format: YYYY-MM-DD
    std::string output_file; // Optional output file path
    
    // Source-specific options
    std::string api_key;
    std::string api_secret;
    bool adjusted = true;    // Adjust for splits/dividends
    bool extended_hours = false;
};

// Download progress callback
using ProgressCallback = std::function<void(const std::string&, int, int)>; // symbol, current, total

// Data validation result
struct ValidationResult {
    bool valid;
    std::string error_message;
    size_t total_points;
    size_t duplicate_points;
    size_t missing_points;
    std::string time_range;
};

class DataDownloader {
public:
    DataDownloader();
    ~DataDownloader();
    
    // Configuration
    bool initialize();
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
    
    // Download operations
    bool download_symbol_data(const DataRequest& request);
    bool download_multiple_symbols(const std::vector<DataRequest>& requests);
    
    // Batch download with common parameters
    bool download_symbol_list(const std::vector<std::string>& symbols,
                            DataSource source,
                            TimeInterval interval,
                            const std::string& start_date,
                            const std::string& end_date,
                            const std::string& output_dir);
    
    // Data validation and processing
    ValidationResult validate_data_file(const std::string& file_path);
    bool merge_data_files(const std::vector<std::string>& input_files,
                         const std::string& output_file);
    bool convert_data_format(const std::string& input_file,
                           const std::string& output_file,
                           const std::string& input_format,
                           const std::string& output_format);
    
    // Utility functions
    static std::string interval_to_string(TimeInterval interval);
    static TimeInterval string_to_interval(const std::string& interval_str);
    static std::string source_to_string(DataSource source);
    static DataSource string_to_source(const std::string& source_str);
    
    // Data source availability
    bool is_source_available(DataSource source) const;
    std::vector<std::string> get_supported_symbols(DataSource source) const;

    // Simplified data client classes - full implementation would be added later
    class AlpacaDataClient {
    public:
        AlpacaDataClient(const std::string& api_key, const std::string& api_secret) 
            : api_key_(api_key), api_secret_(api_secret) {}
        std::vector<HistoricalDataPoint> get_bars(const std::string&, TimeInterval, 
                                                  const std::string&, const std::string&) { 
            return {}; 
        }
    private:
        std::string api_key_, api_secret_;
    };

    class YahooFinanceClient {
    public:
        YahooFinanceClient() {}
        std::vector<HistoricalDataPoint> get_historical_data(const std::string&, 
                                                            const std::string&, 
                                                            const std::string&, 
                                                            const std::string& = "1d") { 
            return {}; 
        }
    };

    class AlphaVantageClient {
    public:
        AlphaVantageClient(const std::string& api_key) : api_key_(api_key) {}
        std::vector<HistoricalDataPoint> get_intraday_data(const std::string&, const std::string& = "5min") { 
            return {}; 
        }
        std::vector<HistoricalDataPoint> get_daily_data(const std::string&) { 
            return {}; 
        }
    private:
        std::string api_key_;
    };

    class IEXCloudClient {
    public:
        IEXCloudClient(const std::string& api_token) : api_token_(api_token) {}
        std::vector<HistoricalDataPoint> get_historical_prices(const std::string&, const std::string& = "1y") { 
            return {}; 
        }
    private:
        std::string api_token_;
    };

    class PolygonClient {
    public:
        PolygonClient(const std::string& api_key) : api_key_(api_key) {}
        std::vector<HistoricalDataPoint> get_aggregates(const std::string&, int, const std::string&, 
                                                        const std::string&, const std::string&) { 
            return {}; 
        }
    private:
        std::string api_key_;
    };

private:
    Logger logger_;
    ProgressCallback progress_callback_;
    
    // Data source clients (full definitions above)
    
    std::unique_ptr<AlpacaDataClient> alpaca_client_;
    std::unique_ptr<YahooFinanceClient> yahoo_client_;
    std::unique_ptr<AlphaVantageClient> alphavantage_client_;
    std::unique_ptr<IEXCloudClient> iex_client_;
    std::unique_ptr<PolygonClient> polygon_client_;
    
    // Download methods for different sources
    bool download_from_alpaca(const DataRequest& request);
    bool download_from_yahoo(const DataRequest& request);
    bool download_from_alphavantage(const DataRequest& request);
    bool download_from_iex(const DataRequest& request);
    bool download_from_polygon(const DataRequest& request);
    bool load_from_csv(const DataRequest& request);
    
    // Data processing helpers
    bool write_data_to_csv(const std::vector<HistoricalDataPoint>& data,
                          const std::string& file_path);
    std::vector<HistoricalDataPoint> read_data_from_csv(const std::string& file_path);
    bool validate_data_consistency(const std::vector<HistoricalDataPoint>& data);
    void remove_duplicates(std::vector<HistoricalDataPoint>& data);
    void fill_missing_data(std::vector<HistoricalDataPoint>& data, TimeInterval interval);
    
    // HTTP and parsing helpers
    std::string make_http_request(const std::string& url, 
                                 const std::vector<std::string>& headers = {});
    std::vector<HistoricalDataPoint> parse_json_response(const std::string& response,
                                                        DataSource source,
                                                        const std::string& symbol);
    
    // Date/time utilities
    uint64_t parse_date_string(const std::string& date_str);
    std::string timestamp_to_date_string(uint64_t timestamp);
    bool is_market_day(uint64_t timestamp);
    int get_interval_minutes(TimeInterval interval);
    
    // Rate limiting
    void respect_rate_limits(DataSource source);
    std::unordered_map<DataSource, uint64_t> last_request_times_;
    std::unordered_map<DataSource, int> request_counts_;
    
    // Configuration
    struct DataSourceConfig {
        std::string base_url;
        int rate_limit_requests_per_minute;
        int rate_limit_requests_per_day;
        std::vector<std::string> required_headers;
        bool requires_api_key;
    };
    
    std::unordered_map<DataSource, DataSourceConfig> source_configs_;
    void initialize_source_configs();

};

} // namespace hft