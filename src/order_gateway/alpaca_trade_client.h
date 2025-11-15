#pragma once

#include "../common/websocket_client.h"
#include "../common/message_types.h"
#include <functional>
#include <unordered_map>

namespace hft {

// Alpaca trade update event
struct AlpacaTradeUpdate {
    std::string event;          // fill, partial_fill, canceled, etc.
    std::string order_id;       // Alpaca order ID
    std::string client_order_id;// Client order ID
    std::string symbol;
    std::string side;           // buy/sell
    double qty = 0.0;
    double price = 0.0;
    double filled_qty = 0.0;
    double fill_price = 0.0;
    std::string timestamp;
    
    bool is_fill() const { return event == "fill" || event == "partial_fill"; }
    bool is_canceled() const { return event == "canceled"; }
    bool is_rejected() const { return event == "rejected"; }
};

class AlpacaTradeClient : public WebSocketClient {
public:
    AlpacaTradeClient();
    virtual ~AlpacaTradeClient() = default;
    
    // Initialize with Alpaca credentials
    bool initialize_alpaca(const std::string& api_key, const std::string& api_secret, 
                          const std::string& websocket_url, const std::string& host,
                          bool paper_trading = true);
    
    // Set callback for trade updates
    void set_trade_callback(std::function<void(const AlpacaTradeUpdate&)> callback);
    
    // Map internal order ID to external order ID for tracking
    void map_order(uint64_t internal_order_id, const std::string& external_order_id);
    void unmap_order(uint64_t internal_order_id);

protected:
    // WebSocketClient overrides
    std::string create_auth_message() override;
    bool authenticate_after_connect() override;
    void on_connected() override;

private:
    // Credentials
    std::string api_key_;
    std::string api_secret_;
    bool paper_trading_ = true;
    
    // Callbacks
    std::function<void(const AlpacaTradeUpdate&)> trade_callback_;
    
    // Order mapping (internal ID -> external ID)
    std::unordered_map<uint64_t, std::string> order_map_;
    std::unordered_map<std::string, uint64_t> reverse_order_map_;
    
    // Message processing
    void process_message(const std::string& message);
    void handle_trade_update(const nlohmann::json& update);
    AlpacaTradeUpdate parse_trade_update(const nlohmann::json& data);
    std::string create_subscription_message();
};

} // namespace hft