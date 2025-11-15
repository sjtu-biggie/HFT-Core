#pragma once

#include "logging.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <nlohmann/json.hpp>

namespace hft {

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class WebSocketClient {
public:
    WebSocketClient(const std::string& client_name);
    virtual ~WebSocketClient();

    // Connection management
    bool initialize(const std::string& websocket_url, const std::string& host, const std::string& port = "443");
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_.load(); }

    // Message handling
    bool send_message(const std::string& message);
    void set_message_callback(std::function<void(const std::string&)> callback);
    
    // Control
    void start();
    void stop();
    bool is_running() const { return running_.load(); }

protected:
    // Virtual methods for customization
    virtual std::string create_auth_message() = 0;
    virtual bool authenticate_after_connect() { return true; }
    virtual void on_connected() {}
    virtual void on_disconnected() {}

private:
    // Connection state
    std::string client_name_;
    std::string websocket_url_;
    std::string host_;
    std::string path_;
    std::string port_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    
    // Callback
    std::function<void(const std::string&)> message_callback_;
    
    // Boost.Beast WebSocket connection
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<ssl::context> ssl_ctx_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>> ws_;
    std::unique_ptr<std::thread> ws_thread_;
    beast::flat_buffer buffer_;
    
    // Logger
    mutable Logger logger_;
    
    // WebSocket connection methods
    bool establish_websocket_connection();
    void websocket_thread_func();
    void close_websocket_connection();
    std::string extract_path_from_url(const std::string& url);
};

} // namespace hft