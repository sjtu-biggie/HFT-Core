#include "alpaca_trade_client.h"
#include "../common/static_config.h"

namespace hft {

AlpacaTradeClient::AlpacaTradeClient() 
    : WebSocketClient("AlpacaTradeClient") {
    
    // Set message callback to handle incoming messages
    set_message_callback([this](const std::string& message) {
        process_message(message);
    });
}

bool AlpacaTradeClient::initialize_alpaca(const std::string& api_key, const std::string& api_secret, 
                                         const std::string& websocket_url, const std::string& host,
                                         bool paper_trading) {
    api_key_ = api_key;
    api_secret_ = api_secret;
    paper_trading_ = paper_trading;
    
    return initialize(websocket_url, host);
}

void AlpacaTradeClient::set_trade_callback(std::function<void(const AlpacaTradeUpdate&)> callback) {
    trade_callback_ = callback;
}

void AlpacaTradeClient::map_order(uint64_t internal_order_id, const std::string& external_order_id) {
    order_map_[internal_order_id] = external_order_id;
    reverse_order_map_[external_order_id] = internal_order_id;
}

void AlpacaTradeClient::unmap_order(uint64_t internal_order_id) {
    auto it = order_map_.find(internal_order_id);
    if (it != order_map_.end()) {
        reverse_order_map_.erase(it->second);
        order_map_.erase(it);
    }
}

std::string AlpacaTradeClient::create_auth_message() {
    nlohmann::json auth;
    auth["action"] = "auth";
    auth["key"] = api_key_;
    auth["secret"] = api_secret_;
    return auth.dump();
}

bool AlpacaTradeClient::authenticate_after_connect() {
    // Send authentication message
    std::string auth_msg = create_auth_message();
    if (!send_message(auth_msg)) {
        return false;
    }
    
    // Wait for authentication response
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Send subscription message for trade updates
    std::string sub_msg = create_subscription_message();
    if (!send_message(sub_msg)) {
        return false;
    }
    
    return true;
}

void AlpacaTradeClient::on_connected() {
    // Additional setup after connection if needed
}

std::string AlpacaTradeClient::create_subscription_message() {
    nlohmann::json sub;
    sub["action"] = "listen";
    sub["data"]["streams"] = nlohmann::json::array({"trade_updates"});
    return sub.dump();
}

void AlpacaTradeClient::process_message(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        
        // Handle different message types
        if (json.is_array()) {
            for (const auto& msg : json) {
                if (msg.contains("stream") && msg["stream"] == "trade_updates") {
                    handle_trade_update(msg);
                }
            }
        } else if (json.is_object()) {
            if (json.contains("stream") && json["stream"] == "trade_updates") {
                handle_trade_update(json);
            } else if (json.contains("msg")) {
                // Handle status messages
                std::string status_msg = json["msg"];
                if (status_msg == "connected") {
                    // Connection confirmed
                } else if (status_msg == "authenticated") {
                    // Authentication confirmed
                }
            }
        }
        
    } catch (const std::exception& e) {
        // Error parsing message
    }
}

void AlpacaTradeClient::handle_trade_update(const nlohmann::json& update) {
    try {
        if (update.contains("data")) {
            AlpacaTradeUpdate trade_update = parse_trade_update(update["data"]);
            
            if (trade_callback_) {
                trade_callback_(trade_update);
            }
        }
    } catch (const std::exception& e) {
        // Error handling trade update
    }
}

AlpacaTradeUpdate AlpacaTradeClient::parse_trade_update(const nlohmann::json& data) {
    AlpacaTradeUpdate update;
    
    update.event = data.value("event", "");
    update.order_id = data.value("order", nlohmann::json{}).value("id", "");
    update.client_order_id = data.value("order", nlohmann::json{}).value("client_order_id", "");
    update.symbol = data.value("order", nlohmann::json{}).value("symbol", "");
    update.side = data.value("order", nlohmann::json{}).value("side", "");
    
    // Parse quantities and prices
    if (data.contains("order")) {
        const auto& order = data["order"];
        
        try {
            if (order.contains("qty") && order["qty"].is_string()) {
                update.qty = std::stod(order["qty"].get<std::string>());
            }
            if (order.contains("limit_price") && order["limit_price"].is_string()) {
                update.price = std::stod(order["limit_price"].get<std::string>());
            }
            if (order.contains("filled_qty") && order["filled_qty"].is_string()) {
                update.filled_qty = std::stod(order["filled_qty"].get<std::string>());
            }
        } catch (const std::exception& e) {
            // Error parsing numeric fields
        }
    }
    
    // Parse fill price if it's a fill event
    if (update.is_fill() && data.contains("price") && data["price"].is_string()) {
        try {
            update.fill_price = std::stod(data["price"].get<std::string>());
        } catch (const std::exception& e) {
            // Error parsing fill price
        }
    }
    
    update.timestamp = data.value("timestamp", "");
    
    return update;
}

} // namespace hft