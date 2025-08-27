#include "alpaca_client.h"
#include "../common/static_config.h"
#include <curl/curl.h>
#include <json/json.h>
#include <sstream>
#include <cstring>

namespace hft {

// Callback for curl to write response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    } catch(std::bad_alloc &e) {
        return 0;
    }
    return newLength;
}

AlpacaClient::AlpacaClient() 
    : connected_(false)
    , logger_("AlpacaClient", StaticConfig::get_logger_endpoint()) {
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

AlpacaClient::~AlpacaClient() {
    curl_global_cleanup();
}

bool AlpacaClient::initialize(const std::string& api_key, const std::string& api_secret, 
                             const std::string& base_url) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    base_url_ = base_url;
    
    logger_.info("Initializing Alpaca client with base URL: " + base_url);
    
    // Test connection with account info
    try {
        std::string response = make_http_request("GET", "/v2/account");
        if (!response.empty()) {
            Json::Value root;
            Json::CharReaderBuilder builder;
            std::string errors;
            
            std::istringstream iss(response);
            if (Json::parseFromStream(builder, iss, &root, &errors)) {
                if (root.isMember("id")) {
                    connected_ = true;
                    logger_.info("Successfully connected to Alpaca API");
                    return true;
                } else if (root.isMember("message")) {
                    logger_.error("Alpaca API error: " + root["message"].asString());
                }
            } else {
                logger_.error("Failed to parse Alpaca response: " + errors);
            }
        }
    } catch (const std::exception& e) {
        logger_.error("Alpaca initialization failed: " + std::string(e.what()));
    }
    
    connected_ = false;
    return false;
}

AlpacaOrderResponse AlpacaClient::submit_market_order(const std::string& symbol, 
                                                     const std::string& side,
                                                     double quantity) {
    AlpacaOrderResponse response{};
    
    if (!connected_) {
        response.error_message = "Alpaca client not connected";
        return response;
    }
    
    Json::Value order;
    order["symbol"] = symbol;
    order["qty"] = quantity;
    order["side"] = side;
    order["type"] = "market";
    order["time_in_force"] = "day";
    
    Json::StreamWriterBuilder builder;
    std::string payload = Json::writeString(builder, order);
    
    std::string api_response = make_http_request("POST", "/v2/orders", payload);
    return parse_order_response(api_response);
}

AlpacaOrderResponse AlpacaClient::submit_limit_order(const std::string& symbol, 
                                                    const std::string& side,
                                                    double quantity, 
                                                    double limit_price) {
    AlpacaOrderResponse response{};
    
    if (!connected_) {
        response.error_message = "Alpaca client not connected";
        return response;
    }
    
    Json::Value order;
    order["symbol"] = symbol;
    order["qty"] = quantity;
    order["side"] = side;
    order["type"] = "limit";
    order["limit_price"] = limit_price;
    order["time_in_force"] = "day";
    
    Json::StreamWriterBuilder builder;
    std::string payload = Json::writeString(builder, order);
    
    std::string api_response = make_http_request("POST", "/v2/orders", payload);
    return parse_order_response(api_response);
}

AlpacaOrderResponse AlpacaClient::get_order_status(const std::string& order_id) {
    AlpacaOrderResponse response{};
    
    if (!connected_) {
        response.error_message = "Alpaca client not connected";
        return response;
    }
    
    std::string endpoint = "/v2/orders/" + order_id;
    std::string api_response = make_http_request("GET", endpoint);
    return parse_order_response(api_response);
}

AlpacaOrderResponse AlpacaClient::cancel_order(const std::string& order_id) {
    AlpacaOrderResponse response{};
    
    if (!connected_) {
        response.error_message = "Alpaca client not connected";
        return response;
    }
    
    std::string endpoint = "/v2/orders/" + order_id;
    std::string api_response = make_http_request("DELETE", endpoint);
    return parse_order_response(api_response);
}

bool AlpacaClient::is_market_open() {
    if (!connected_) return false;
    
    try {
        std::string response = make_http_request("GET", "/v2/clock");
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        std::istringstream iss(response);
        if (Json::parseFromStream(builder, iss, &root, &errors)) {
            if (root.isMember("is_open")) {
                return root["is_open"].asBool();
            }
        }
    } catch (const std::exception& e) {
        logger_.error("Failed to check market status: " + std::string(e.what()));
    }
    
    return false;
}

double AlpacaClient::get_buying_power() {
    if (!connected_) return 0.0;
    
    try {
        std::string response = make_http_request("GET", "/v2/account");
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        std::istringstream iss(response);
        if (Json::parseFromStream(builder, iss, &root, &errors)) {
            if (root.isMember("buying_power")) {
                return std::stod(root["buying_power"].asString());
            }
        }
    } catch (const std::exception& e) {
        logger_.error("Failed to get buying power: " + std::string(e.what()));
    }
    
    return 0.0;
}

std::string AlpacaClient::make_http_request(const std::string& method, 
                                          const std::string& endpoint,
                                          const std::string& payload) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        logger_.error("Failed to initialize curl");
        return "";
    }
    
    std::string response_string;
    std::string full_url = base_url_ + endpoint;
    
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // Set headers
    struct curl_slist* headers = nullptr;
    std::string auth_header = get_auth_header();
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Set method and payload
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (!payload.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        }
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        logger_.error("HTTP request failed: " + std::string(curl_easy_strerror(res)));
        return "";
    }
    
    if (response_code >= 400) {
        logger_.error("HTTP error " + std::to_string(response_code) + ": " + response_string);
    }
    
    return response_string;
}

std::string AlpacaClient::get_auth_header() {
    return "APCA-API-KEY-ID: " + api_key_ + "\r\nAPCA-API-SECRET-KEY: " + api_secret_;
}

AlpacaOrderResponse AlpacaClient::parse_order_response(const std::string& response) {
    AlpacaOrderResponse result{};
    
    if (response.empty()) {
        result.error_message = "Empty response from Alpaca API";
        return result;
    }
    
    try {
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        
        std::istringstream iss(response);
        if (!Json::parseFromStream(builder, iss, &root, &errors)) {
            result.error_message = "Failed to parse JSON: " + errors;
            return result;
        }
        
        // Check for API error
        if (root.isMember("message")) {
            result.error_message = root["message"].asString();
            return result;
        }
        
        // Parse successful response
        if (root.isMember("id")) {
            result.order_id = root["id"].asString();
        }
        if (root.isMember("client_order_id")) {
            result.client_order_id = root["client_order_id"].asString();
        }
        if (root.isMember("symbol")) {
            result.symbol = root["symbol"].asString();
        }
        if (root.isMember("status")) {
            std::string status_str = root["status"].asString();
            // Convert string status to enum
            if (status_str == "new") result.status = AlpacaOrderStatus::NEW;
            else if (status_str == "partially_filled") result.status = AlpacaOrderStatus::PARTIALLY_FILLED;
            else if (status_str == "filled") result.status = AlpacaOrderStatus::FILLED;
            else if (status_str == "canceled") result.status = AlpacaOrderStatus::CANCELED;
            else if (status_str == "rejected") result.status = AlpacaOrderStatus::REJECTED;
        }
        if (root.isMember("side")) {
            result.side = root["side"].asString();
        }
        if (root.isMember("qty")) {
            result.quantity = std::stod(root["qty"].asString());
        }
        if (root.isMember("filled_qty")) {
            result.filled_qty = std::stod(root["filled_qty"].asString());
        }
        if (root.isMember("filled_avg_price")) {
            result.fill_price = std::stod(root["filled_avg_price"].asString());
        }
        
    } catch (const std::exception& e) {
        result.error_message = "Exception parsing response: " + std::string(e.what());
    }
    
    return result;
}

std::string AlpacaClient::convert_signal_action_to_side(SignalAction action) {
    return (action == SignalAction::BUY) ? "buy" : "sell";
}

AlpacaOrderType AlpacaClient::convert_order_type(OrderType type) {
    switch (type) {
        case OrderType::MARKET:
            return AlpacaOrderType::MARKET;
        case OrderType::LIMIT:
            return AlpacaOrderType::LIMIT;
        default:
            return AlpacaOrderType::MARKET;
    }
}

} // namespace hft