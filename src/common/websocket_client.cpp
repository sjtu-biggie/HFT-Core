#include "websocket_client.h"
#include "static_config.h"

namespace hft {

WebSocketClient::WebSocketClient(const std::string& client_name) 
    : client_name_(client_name)
    , logger_(client_name, StaticConfig::get_logger_endpoint()) {
}

WebSocketClient::~WebSocketClient() {
    stop();
}

bool WebSocketClient::initialize(const std::string& websocket_url, const std::string& host, const std::string& port) {
    websocket_url_ = websocket_url;
    host_ = host;
    port_ = port;
    
    // Extract path from websocket_url
    path_ = extract_path_from_url(websocket_url);
    
    logger_.info(client_name_ + " initialized");
    logger_.info("WebSocket URL: " + websocket_url_);
    logger_.info("WebSocket Host: " + host_);
    logger_.info("WebSocket Path: " + path_);
    
    return true;
}

bool WebSocketClient::connect() {
    if (connected_.load()) {
        logger_.warning("Already connected to WebSocket");
        return true;
    }
    
    logger_.info("Connecting to WebSocket: " + websocket_url_);
    
    try {
        // Initialize io_context and SSL context
        ioc_ = std::make_unique<net::io_context>();
        ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
        
        // Configure SSL context
        ssl_ctx_->set_default_verify_paths();
        ssl_ctx_->set_verify_mode(ssl::verify_none); // For development - should use verify_peer in production
        
        if (!establish_websocket_connection()) {
            logger_.error("Failed to establish WebSocket connection");
            return false;
        }
        
        // Start WebSocket thread
        ws_thread_ = std::make_unique<std::thread>([this]() {
            websocket_thread_func();
        });
        
        // Give the WebSocket thread a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Authenticate if needed
        if (!authenticate_after_connect()) {
            logger_.error("Authentication failed");
            return false;
        }
        
        on_connected();
        logger_.info("Successfully connected to WebSocket");
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("Connection failed: " + std::string(e.what()));
        return false;
    }
}

void WebSocketClient::disconnect() {
    if (!connected_.load()) return;
    
    logger_.info("Disconnecting from WebSocket");
    
    connected_ = false;
    close_websocket_connection();
    
    if (ws_thread_ && ws_thread_->joinable()) {
        ws_thread_->join();
    }
    
    on_disconnected();
    logger_.info("Disconnected from WebSocket");
}

void WebSocketClient::start() {
    if (running_.load()) {
        logger_.warning("Already running");
        return;
    }
    
    running_ = true;
    logger_.info(client_name_ + " started");
}

void WebSocketClient::stop() {
    if (!running_.load()) return;
    
    running_ = false;
    disconnect();
    logger_.info(client_name_ + " stopped");
}

bool WebSocketClient::send_message(const std::string& message) {
    if (!ws_) {
        logger_.error("WebSocket not initialized");
        return false;
    }
    
    if (!connected_.load()) {
        logger_.error("WebSocket not connected");
        return false;
    }
    
    try {
        logger_.debug("Sending message: " + message.substr(0, std::min(100UL, message.length())));
        ws_->write(net::buffer(message));
        return true;
    } catch (const std::exception& e) {
        logger_.error("Exception while sending message: " + std::string(e.what()));
        connected_ = false;
        return false;
    }
}

void WebSocketClient::set_message_callback(std::function<void(const std::string&)> callback) {
    message_callback_ = callback;
}

bool WebSocketClient::establish_websocket_connection() {
    logger_.info("Establishing WebSocket connection to: " + websocket_url_);
    
    try {
        // Create the websocket stream
        ws_ = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(*ioc_, *ssl_ctx_);
        
        // Set SNI hostname
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
            logger_.error("Failed to set SNI hostname");
            return false;
        }
        
        // Look up the domain name
        tcp::resolver resolver(*ioc_);
        auto const results = resolver.resolve(host_, port_);
        
        // Make the connection on the IP address we get from a lookup
        auto ep = beast::get_lowest_layer(*ws_).connect(results);
        
        // Update the host string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        std::string host_with_port = host_ + ":" + std::to_string(ep.port());
        
        // Perform the SSL handshake
        ws_->next_layer().handshake(ssl::stream_base::client);
        
        // Set a decorator to change the User-Agent of the handshake
        ws_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::user_agent, "HFT-System/1.0");
            }));
        
        // Perform the websocket handshake
        ws_->handshake(host_with_port, path_);
        
        logger_.info("WebSocket connection established successfully");
        connected_ = true;
        
        return true;
        
    } catch (const std::exception& e) {
        logger_.error("Failed to establish WebSocket connection: " + std::string(e.what()));
        connected_ = false;
        return false;
    }
}

void WebSocketClient::websocket_thread_func() {
    logger_.info("WebSocket thread started");
    
    while (connected_.load() && running_.load()) {
        try {
            // Read a message into our buffer
            buffer_.clear();
            ws_->read(buffer_);
            
            // Convert to string
            std::string message = beast::buffers_to_string(buffer_.data());
            logger_.debug("Received message: " + message.substr(0, 500));
            
            // Process the message through callback
            if (message_callback_) {
                message_callback_(message);
            }
            
        } catch (beast::system_error const& se) {
            logger_.error("Beast system error in WebSocket thread: " + se.code().message());
            
            if (se.code() != websocket::error::closed) {
                logger_.error("Non-close WebSocket error");
            } else {
                logger_.info("WebSocket closed normally");
            }
            break;
        } catch (const std::exception& e) {
            logger_.error("General exception in WebSocket thread: " + std::string(e.what()));
            break;
        }
    }
    
    logger_.info("WebSocket thread terminated");
    connected_ = false;
}

void WebSocketClient::close_websocket_connection() {
    if (ws_ && connected_.load()) {
        try {
            ws_->close(websocket::close_code::normal);
        } catch (const std::exception& e) {
            logger_.debug("Error closing WebSocket: " + std::string(e.what()));
        }
    }
    
    if (ioc_) {
        ioc_->stop();
    }
    
    connected_ = false;
}

std::string WebSocketClient::extract_path_from_url(const std::string& url) {
    // URL format: ws://host:port/path or wss://host:port/path
    size_t path_start = url.find('/', 8); // Skip "wss://" or "ws://"
    if (path_start != std::string::npos) {
        return url.substr(path_start);
    }
    return "/"; // Default path
}

} // namespace hft