#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include <zmq.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <string>
#include <memory>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <map>
#include <vector>
#include <cstring>
#include <algorithm>

namespace hft {

class ControlAPI {
public:
    ControlAPI() 
        : running_(false)
        , logger_("ControlAPI", StaticConfig::get_logger_endpoint())
        , context_(1)
        , zmq_publisher_(context_, ZMQ_PUB)
        , server_socket_(-1)
        , port_(8081)
        , api_key_(get_api_key_from_env()) {
        
        // Set socket options for publishing
        int linger = 0;
        zmq_publisher_.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    }
    
    ~ControlAPI() {
        stop();
    }
    
    bool initialize() {
        try {
            // Bind ZMQ publisher to send control commands
            std::string zmq_endpoint = "tcp://*:5560";  // Control command endpoint
            zmq_publisher_.bind(zmq_endpoint);
            logger_.info("ZMQ publisher bound to: " + zmq_endpoint);
            
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
            server_addr.sin_addr.s_addr = INADDR_LOOPBACK;  // Only localhost access
            server_addr.sin_port = htons(port_);
            
            if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                logger_.error("Failed to bind to port " + std::to_string(port_));
                close(server_socket_);
                return false;
            }
            
            if (listen(server_socket_, 5) < 0) {
                logger_.error("Failed to listen on socket");
                close(server_socket_);
                return false;
            }
            
            logger_.info("Control API listening on localhost:" + std::to_string(port_));
            return true;
            
        } catch (const std::exception& e) {
            logger_.error("Initialization failed: " + std::string(e.what()));
            return false;
        }
    }
    
    void start() {
        running_ = true;
        
        // Start HTTP server thread
        server_thread_ = std::make_unique<std::thread>(&ControlAPI::server_loop, this);
        
        logger_.info("Control API started");
    }
    
    void stop() {
        running_ = false;
        
        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }
        
        if (server_thread_ && server_thread_->joinable()) {
            server_thread_->join();
        }
        
        logger_.info("Control API stopped");
    }
    
    bool is_running() const {
        return running_;
    }

private:
    std::atomic<bool> running_;
    Logger logger_;
    zmq::context_t context_;
    zmq::socket_t zmq_publisher_;
    int server_socket_;
    int port_;
    std::string api_key_;
    
    std::unique_ptr<std::thread> server_thread_;
    
    // Security helpers
    static std::string get_api_key_from_env() {
        const char* env_key = std::getenv("HFT_API_KEY");
        if (env_key && strlen(env_key) > 0) {
            return std::string(env_key);
        }
        // Fallback for development only - log warning
        std::cerr << "WARNING: HFT_API_KEY environment variable not set. Using default key." << std::endl;
        std::cerr << "         For production, set: export HFT_API_KEY=your-secure-key" << std::endl;
        return "hft-control-key-2025";  // Development fallback
    }
    
    static constexpr size_t MAX_REQUEST_SIZE = 8192;  // 8KB limit
    
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
            
            // Handle client request
            handle_request(client_socket);
            close(client_socket);
        }
        
        logger_.info("HTTP server thread stopped");
    }
    
    void handle_request(int client_socket) {
        // Set socket timeout for security
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        
        std::vector<char> buffer(MAX_REQUEST_SIZE);
        ssize_t bytes_read = read(client_socket, buffer.data(), buffer.size() - 1);
        
        if (bytes_read <= 0) {
            send_response(client_socket, 400, "Bad Request", "Invalid request");
            return;
        }
        
        if (static_cast<size_t>(bytes_read) >= buffer.size() - 1) {
            send_response(client_socket, 413, "Request Entity Too Large", "Request too large");
            return;
        }
        
        buffer[bytes_read] = '\0';  // Null terminate
        std::string request(buffer.data(), bytes_read);
        
        // Parse HTTP request
        auto request_info = parse_http_request(request);
        
        // Authenticate
        if (!authenticate(request_info)) {
            send_response(client_socket, 401, "Unauthorized", "Invalid API key");
            return;
        }
        
        // Route the request
        route_request(client_socket, request_info);
    }
    
    struct HTTPRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::string body;
    };
    
    HTTPRequest parse_http_request(const std::string& request) {
        HTTPRequest req;
        std::istringstream stream(request);
        std::string line;
        
        // Parse request line
        if (std::getline(stream, line)) {
            std::istringstream line_stream(line);
            line_stream >> req.method >> req.path;
        }
        
        // Parse headers
        while (std::getline(stream, line) && line != "\r" && !line.empty()) {
            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);
                
                // Trim whitespace
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                
                req.headers[key] = value;
            }
        }
        
        // Parse body (remaining content)
        std::string remaining_content;
        std::string body_line;
        while (std::getline(stream, body_line)) {
            remaining_content += body_line + "\n";
        }
        req.body = remaining_content;
        
        return req;
    }
    
    bool authenticate(const HTTPRequest& req) {
        auto auth_header = req.headers.find("X-API-Key");
        if (auth_header == req.headers.end()) {
            return false;
        }
        return auth_header->second == api_key_;
    }
    
    void route_request(int client_socket, const HTTPRequest& req) {
        if (req.method == "POST") {
            if (req.path == "/api/start") {
                handle_start_command(client_socket);
            } else if (req.path == "/api/stop") {
                handle_stop_command(client_socket);
            } else if (req.path == "/api/emergency_stop") {
                handle_emergency_stop_command(client_socket);
            } else if (req.path == "/api/liquidate") {
                handle_liquidate_command(client_socket);
            } else {
                send_response(client_socket, 404, "Not Found", "Endpoint not found");
            }
        } else if (req.method == "GET") {
            if (req.path == "/api/status") {
                handle_status_request(client_socket);
            } else {
                send_response(client_socket, 404, "Not Found", "Endpoint not found");
            }
        } else {
            send_response(client_socket, 405, "Method Not Allowed", "Method not supported");
        }
    }
    
    void handle_start_command(int client_socket) {
        // Send control command to start trading
        ControlCommand cmd{};
        cmd.header = MessageFactory::create_header(MessageType::CONTROL_COMMAND, sizeof(cmd));
        cmd.action = ControlAction::START_TRADING;
        std::strncpy(cmd.parameters, "{\"action\":\"start\"}", sizeof(cmd.parameters) - 1);
        
        send_zmq_command(cmd);
        
        logger_.info("Sent START_TRADING command");
        send_json_response(client_socket, 200, "OK", "{\"status\":\"success\",\"message\":\"Trading started\"}");
    }
    
    void handle_stop_command(int client_socket) {
        // Send control command to stop trading
        ControlCommand cmd{};
        cmd.header = MessageFactory::create_header(MessageType::CONTROL_COMMAND, sizeof(cmd));
        cmd.action = ControlAction::STOP_TRADING;
        std::strncpy(cmd.parameters, "{\"action\":\"stop\"}", sizeof(cmd.parameters) - 1);
        
        send_zmq_command(cmd);
        
        logger_.info("Sent STOP_TRADING command");
        send_json_response(client_socket, 200, "OK", "{\"status\":\"success\",\"message\":\"Trading stopped\"}");
    }
    
    void handle_emergency_stop_command(int client_socket) {
        // Send emergency stop command
        ControlCommand cmd{};
        cmd.header = MessageFactory::create_header(MessageType::CONTROL_COMMAND, sizeof(cmd));
        cmd.action = ControlAction::EMERGENCY_STOP;
        std::strncpy(cmd.parameters, "{\"action\":\"emergency_stop\"}", sizeof(cmd.parameters) - 1);
        
        send_zmq_command(cmd);
        
        logger_.info("Sent EMERGENCY_STOP command");
        send_json_response(client_socket, 200, "OK", "{\"status\":\"success\",\"message\":\"Emergency stop executed\"}");
    }
    
    void handle_liquidate_command(int client_socket) {
        // Send liquidate all positions command
        ControlCommand cmd{};
        cmd.header = MessageFactory::create_header(MessageType::CONTROL_COMMAND, sizeof(cmd));
        cmd.action = ControlAction::LIQUIDATE_ALL;
        std::strncpy(cmd.parameters, "{\"action\":\"liquidate_all\"}", sizeof(cmd.parameters) - 1);
        
        send_zmq_command(cmd);
        
        logger_.info("Sent LIQUIDATE_ALL command");
        send_json_response(client_socket, 200, "OK", "{\"status\":\"success\",\"message\":\"All positions liquidated\"}");
    }
    
    void handle_status_request(int client_socket) {
        // Return system status
        std::ostringstream status_json;
        status_json << "{";
        status_json << "\"status\":\"active\",";
        status_json << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << ",";
        status_json << "\"version\":\"2.0\",";
        status_json << "\"available_endpoints\":[\"start\",\"stop\",\"emergency_stop\",\"liquidate\",\"status\"]";
        status_json << "}";
        
        send_json_response(client_socket, 200, "OK", status_json.str());
    }
    
    void send_zmq_command(const ControlCommand& cmd) {
        try {
            zmq::message_t msg(sizeof(cmd));
            std::memcpy(msg.data(), &cmd, sizeof(cmd));
            zmq_publisher_.send(msg, zmq::send_flags::dontwait);
        } catch (const std::exception& e) {
            logger_.error("Failed to send ZMQ command: " + std::string(e.what()));
        }
    }
    
    void send_response(int client_socket, int status_code, const std::string& status_text, const std::string& body) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
        response << "Content-Type: text/plain\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "\r\n";
        response << body;
        
        std::string response_str = response.str();
        send(client_socket, response_str.c_str(), response_str.length(), 0);
    }
    
    void send_json_response(int client_socket, int status_code, const std::string& status_text, const std::string& json_body) {
        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n";
        response << "Content-Type: application/json\r\n";
        response << "Access-Control-Allow-Origin: *\r\n";
        response << "Content-Length: " << json_body.length() << "\r\n";
        response << "\r\n";
        response << json_body;
        
        std::string response_str = response.str();
        send(client_socket, response_str.c_str(), response_str.length(), 0);
    }
};

} // namespace hft

// Global instance
std::unique_ptr<hft::ControlAPI> g_control_api;

void start_control_api() {
    g_control_api = std::make_unique<hft::ControlAPI>();
    
    if (!g_control_api->initialize()) {
        throw std::runtime_error("Failed to initialize Control API");
    }
    
    g_control_api->start();
}

void stop_control_api() {
    if (g_control_api) {
        g_control_api->stop();
        g_control_api.reset();
    }
}

bool is_control_api_running() {
    return g_control_api && g_control_api->is_running();
}