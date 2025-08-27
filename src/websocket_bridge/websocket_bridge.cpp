#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include "../common/prometheus_exporter.h"
#include <zmq.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <map>

namespace hft {

class WebSocketBridge {
public:
    WebSocketBridge() 
        : running_(false)
        , logger_("WebSocketBridge", StaticConfig::get_logger_endpoint())
        , context_(1)
        , zmq_subscriber_(context_, ZMQ_SUB)
        , server_socket_(-1)
        , port_(8080) {
        
        // Subscribe to all messages
        zmq_subscriber_.setsockopt(ZMQ_SUBSCRIBE, "", 0);
        zmq_subscriber_.setsockopt(ZMQ_RCVTIMEO, 1000); // 1 second timeout
    }
    
    ~WebSocketBridge() {
        stop();
    }
    
    bool initialize() {
        try {
            // Connect to ZMQ message bus
            std::string zmq_endpoint = StaticConfig::get_market_data_endpoint();
            zmq_subscriber_.connect(zmq_endpoint);
            logger_.info("Connected to ZMQ endpoint: " + zmq_endpoint);
            
            // Create HTTP server socket
            server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
            if (server_socket_ < 0) {
                logger_.error("Failed to create server socket");
                return false;
            }
            
            // Set socket options
            int opt = 1;
            setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            
            // Bind to port
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(port_);
            
            if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                logger_.error("Failed to bind to port " + std::to_string(port_));
                close(server_socket_);
                return false;
            }
            
            if (listen(server_socket_, 10) < 0) {
                logger_.error("Failed to listen on socket");
                close(server_socket_);
                return false;
            }
            
            logger_.info("WebSocket bridge listening on port " + std::to_string(port_));
            return true;
            
        } catch (const std::exception& e) {
            logger_.error("Initialization failed: " + std::string(e.what()));
            return false;
        }
    }
    
    void start() {
        running_ = true;
        
        // Start ZMQ message processing thread
        zmq_thread_ = std::make_unique<std::thread>(&WebSocketBridge::zmq_message_loop, this);
        
        // Start HTTP server thread
        server_thread_ = std::make_unique<std::thread>(&WebSocketBridge::server_loop, this);
        
        logger_.info("WebSocket bridge started");
    }
    
    void stop() {
        running_ = false;
        
        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }
        
        if (zmq_thread_ && zmq_thread_->joinable()) {
            zmq_thread_->join();
        }
        
        if (server_thread_ && server_thread_->joinable()) {
            server_thread_->join();
        }
        
        logger_.info("WebSocket bridge stopped");
    }
    
    bool is_running() const {
        return running_;
    }

private:
    std::atomic<bool> running_;
    Logger logger_;
    zmq::context_t context_;
    zmq::socket_t zmq_subscriber_;
    int server_socket_;
    int port_;
    
    std::unique_ptr<std::thread> zmq_thread_;
    std::unique_ptr<std::thread> server_thread_;
    
    // Thread-safe message buffer
    std::mutex message_mutex_;
    std::vector<std::string> message_buffer_;
    static const size_t MAX_MESSAGES = 1000;
    
    void zmq_message_loop() {
        logger_.info("ZMQ message processing thread started");
        
        while (running_) {
            try {
                zmq::message_t msg;
                auto result = zmq_subscriber_.recv(msg, zmq::recv_flags::dontwait);
                
                if (result) {
                    std::string data(static_cast<char*>(msg.data()), msg.size());
                    
                    // Format as JSON for web clients
                    std::string json_msg = format_as_json(data);
                    
                    // Add to message buffer (thread-safe)
                    {
                        std::lock_guard<std::mutex> lock(message_mutex_);
                        message_buffer_.push_back(json_msg);
                        
                        // Keep buffer size manageable
                        if (message_buffer_.size() > MAX_MESSAGES) {
                            message_buffer_.erase(message_buffer_.begin());
                        }
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                
            } catch (const std::exception& e) {
                logger_.error("ZMQ message processing error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        logger_.info("ZMQ message processing thread stopped");
    }
    
    void server_loop() {
        logger_.info("HTTP server thread started");
        
        while (running_) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                if (running_) {
                    logger_.warning("Failed to accept client connection");
                }
                continue;
            }
            
            // Handle client in separate thread for simplicity
            std::thread client_thread(&WebSocketBridge::handle_client, this, client_socket);
            client_thread.detach();
        }
        
        logger_.info("HTTP server thread stopped");
    }
    
    void handle_client(int client_socket) {
        char buffer[1024] = {0};
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            std::string request(buffer, bytes_read);
            
            // Parse request path
            std::string path = parse_request_path(request);
            
            std::string response;
            if (path == "/metrics") {
                // Prometheus metrics endpoint
                response = build_metrics_response();
            } else {
                // Default: return current messages
                response = build_http_response();
            }
            
            send(client_socket, response.c_str(), response.length(), 0);
        }
        
        close(client_socket);
    }
    
    std::string parse_request_path(const std::string& request) {
        std::istringstream stream(request);
        std::string method, path, version;
        stream >> method >> path >> version;
        return path;
    }
    
    std::string build_metrics_response() {
        std::string metrics_data = PrometheusExporter::export_metrics();
        
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\\r\\n";
        response << "Content-Type: " << PrometheusExporter::get_content_type() << "\\r\\n";
        response << "Access-Control-Allow-Origin: *\\r\\n";
        response << "Content-Length: " << metrics_data.length() << "\\r\\n";
        response << "\\r\\n";
        response << metrics_data;
        
        return response.str();
    }
    
    std::string build_http_response() {
        // Get current messages
        std::vector<std::string> messages;
        {
            std::lock_guard<std::mutex> lock(message_mutex_);
            messages = message_buffer_;
        }
        
        // Build JSON response
        std::ostringstream json;
        json << "{\"messages\":[";
        
        for (size_t i = 0; i < messages.size(); ++i) {
            if (i > 0) json << ",";
            json << messages[i];
        }
        
        json << "],\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "}";
        
        std::string body = json.str();
        
        // Build HTTP response
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        
        return response.str();
    }
    
    std::string format_as_json(const std::string& raw_data) {
        // Simple JSON formatting - in production would parse the actual message types
        std::ostringstream json;
        json << "{\"raw_data\":\"";
        
        // Escape special characters
        for (char c : raw_data) {
            if (c == '"') json << "\\\"";
            else if (c == '\\') json << "\\\\";
            else if (c == '\n') json << "\\n";
            else if (c == '\r') json << "\\r";
            else if (c == '\t') json << "\\t";
            else if (c >= 32 && c <= 126) json << c;  // Printable ASCII
        }
        
        json << "\",\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "}";
        
        return json.str();
    }
};

} // namespace hft

// Global instance
std::unique_ptr<hft::WebSocketBridge> g_bridge;

void start_websocket_bridge() {
    g_bridge = std::make_unique<hft::WebSocketBridge>();
    
    if (!g_bridge->initialize()) {
        throw std::runtime_error("Failed to initialize WebSocket bridge");
    }
    
    g_bridge->start();
}

void stop_websocket_bridge() {
    if (g_bridge) {
        g_bridge->stop();
        g_bridge.reset();
    }
}

bool is_websocket_bridge_running() {
    return g_bridge && g_bridge->is_running();
}