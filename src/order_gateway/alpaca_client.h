#pragma once

#include "../common/message_types.h"
#include "../common/logging.h"
#include <string>
#include <memory>
#include <functional>

namespace hft {

// Alpaca order status
enum class AlpacaOrderStatus {
    NEW = 0,
    PARTIALLY_FILLED,
    FILLED,
    DONE_FOR_DAY,
    CANCELED,
    EXPIRED,
    REPLACED,
    PENDING_CANCEL,
    PENDING_REPLACE,
    REJECTED,
    SUSPENDED,
    CALCULATED
};

// Alpaca order type
enum class AlpacaOrderType {
    MARKET,
    LIMIT,
    STOP,
    STOP_LIMIT,
    TRAILING_STOP
};

// Alpaca time in force
enum class AlpacaTimeInForce {
    DAY,
    GTC,  // Good Till Canceled
    OPG,  // Opening
    CLS,  // Closing
    IOC,  // Immediate or Cancel
    FOK   // Fill or Kill
};

// Alpaca order response
struct AlpacaOrderResponse {
    std::string order_id;
    std::string client_order_id;
    std::string symbol;
    std::string asset_class;
    AlpacaOrderStatus status;
    AlpacaOrderType order_type;
    std::string side;  // "buy" or "sell"
    double quantity;
    double filled_qty;
    double fill_price;
    std::string submitted_at;
    std::string filled_at;
    std::string error_message;
    
    bool is_success() const {
        return error_message.empty();
    }
    
    bool is_filled() const {
        return status == AlpacaOrderStatus::FILLED || 
               status == AlpacaOrderStatus::PARTIALLY_FILLED;
    }
};

// Callback for order status updates
using OrderStatusCallback = std::function<void(const AlpacaOrderResponse&)>;

class AlpacaClient {
public:
    AlpacaClient();
    ~AlpacaClient();
    
    // Configuration
    bool initialize(const std::string& api_key, const std::string& api_secret, 
                   const std::string& base_url = "https://paper-api.alpaca.markets");
    
    // Order management
    AlpacaOrderResponse submit_market_order(const std::string& symbol, 
                                           const std::string& side,
                                           double quantity);
    
    AlpacaOrderResponse submit_limit_order(const std::string& symbol, 
                                          const std::string& side,
                                          double quantity, 
                                          double limit_price);
    
    AlpacaOrderResponse get_order_status(const std::string& order_id);
    AlpacaOrderResponse cancel_order(const std::string& order_id);
    
    // Account info
    bool is_market_open();
    double get_buying_power();
    
    // Status
    bool is_connected() const { return connected_; }
    
    // Set callback for real-time order updates (if streaming is enabled)
    void set_order_status_callback(OrderStatusCallback callback) {
        order_status_callback_ = callback;
    }

private:
    std::string api_key_;
    std::string api_secret_;
    std::string base_url_;
    bool connected_;
    
    Logger logger_;
    OrderStatusCallback order_status_callback_;
    
    // HTTP client methods
    std::string make_http_request(const std::string& method, 
                                 const std::string& endpoint,
                                 const std::string& payload = "");
    
    std::string get_auth_header();
    AlpacaOrderResponse parse_order_response(const std::string& response);
    
    // Convert internal types to Alpaca format
    std::string convert_signal_action_to_side(SignalAction action);
    AlpacaOrderType convert_order_type(OrderType type);
};

} // namespace hft