#include "../common/message_types.h"
#include "../common/logging.h"
#include "../common/static_config.h"
#include "../common/prometheus_exporter.h"
#include "../common/metrics_aggregator.h"
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
#include <errno.h>
#include <sstream>
#include <iomanip>
#include <map>
#include <queue>
#include <future>

namespace hft {

class WebSocketBridge {
public:
    WebSocketBridge() 
        : running_(false)
        , logger_("WebSocketBridge", StaticConfig::get_logger_endpoint())
        , context_(1)
        , zmq_subscriber_(context_, ZMQ_SUB)
        , execution_subscriber_(context_, ZMQ_SUB)
        , control_publisher_(context_, ZMQ_PUB)
        , server_socket_(-1)
        , port_(8080)
        , metrics_aggregator_("tcp://localhost:5560") {
        
        // Subscribe to all market data messages
        zmq_subscriber_.setsockopt(ZMQ_SUBSCRIBE, "", 0);
        zmq_subscriber_.setsockopt(ZMQ_RCVTIMEO, 1000); // 1 second timeout
        
        // Subscribe to all execution messages
        execution_subscriber_.setsockopt(ZMQ_SUBSCRIBE, "", 0);
        execution_subscriber_.setsockopt(ZMQ_RCVTIMEO, 1000); // 1 second timeout
        
        // Control publisher setup
        int linger = 0;
        control_publisher_.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    }
    
    ~WebSocketBridge() {
        stop();
    }
    
    bool initialize() {
        try {
            // Initialize metrics aggregator
            if (!metrics_aggregator_.initialize()) {
                logger_.error("Failed to initialize metrics aggregator");
                return false;
            }
            
            // Connect to ZMQ message bus
            std::string zmq_endpoint = StaticConfig::get_market_data_endpoint();
            zmq_subscriber_.connect(zmq_endpoint);
            logger_.info("Connected to market data endpoint: " + zmq_endpoint);
            
            // Connect to executions endpoint
            std::string exec_endpoint = StaticConfig::get_executions_endpoint();
            execution_subscriber_.connect(exec_endpoint);
            logger_.info("Connected to executions endpoint: " + exec_endpoint);
            
            // Bind control publisher - using port 5561 for control commands
            control_publisher_.bind("tcp://*:5561");
            logger_.info("Control publisher bound to tcp://*:5561");
            
            // Create HTTP server socket
            server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
            if (server_socket_ < 0) {
                logger_.error("Failed to create server socket");
                return false;
            }
            
            // Set socket options for robust binding
            int opt = 1;
            if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                logger_.warning("Failed to set SO_REUSEADDR");
            }
            
            // Enable SO_REUSEPORT for better port reuse (Linux-specific)
            #ifdef SO_REUSEPORT
            if (setsockopt(server_socket_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
                logger_.warning("SO_REUSEPORT not supported, continuing without it");
            }
            #endif
            
            // Bind to port with retry logic
            sockaddr_in server_addr{};
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = INADDR_ANY;
            server_addr.sin_port = htons(port_);
            
            int bind_attempts = 3;
            bool bind_success = false;
            
            for (int attempt = 1; attempt <= bind_attempts; ++attempt) {
                if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == 0) {
                    bind_success = true;
                    break;
                }
                
                int bind_errno = errno;
                if (attempt < bind_attempts) {
                    logger_.warning("Bind attempt " + std::to_string(attempt) + " failed (errno: " + 
                                  std::to_string(bind_errno) + "), retrying in " + 
                                  std::to_string(attempt) + " seconds...");
                    std::this_thread::sleep_for(std::chrono::seconds(attempt));
                } else {
                    logger_.error("Failed to bind to port " + std::to_string(port_) + 
                                " after " + std::to_string(bind_attempts) + " attempts (errno: " + 
                                std::to_string(bind_errno) + ")");
                }
            }
            
            if (!bind_success) {
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
        
        // Start metrics aggregator
        metrics_aggregator_.start();
        
        // Start worker threads for client handling
        for (size_t i = 0; i < MAX_WORKER_THREADS; ++i) {
            worker_threads_.emplace_back(&WebSocketBridge::worker_thread, this);
        }
        
        // Start ZMQ message processing thread
        zmq_thread_ = std::make_unique<std::thread>(&WebSocketBridge::zmq_message_loop, this);
        
        // Start execution processing thread
        execution_thread_ = std::make_unique<std::thread>(&WebSocketBridge::execution_message_loop, this);
        
        // Start HTTP server thread
        server_thread_ = std::make_unique<std::thread>(&WebSocketBridge::server_loop, this);
        
        logger_.info("WebSocket bridge started with " + std::to_string(MAX_WORKER_THREADS) + " worker threads");
    }
    
    void stop() {
        running_ = false;
        
        // Stop metrics aggregator
        metrics_aggregator_.stop();
        
        // Notify all worker threads to shutdown
        client_queue_cv_.notify_all();
        
        if (server_socket_ >= 0) {
            close(server_socket_);
            server_socket_ = -1;
        }
        
        if (zmq_thread_ && zmq_thread_->joinable()) {
            zmq_thread_->join();
        }
        
        if (execution_thread_ && execution_thread_->joinable()) {
            execution_thread_->join();
        }
        
        if (server_thread_ && server_thread_->joinable()) {
            server_thread_->join();
        }
        
        // Join all worker threads
        for (auto& worker : worker_threads_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        worker_threads_.clear();
        
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
    zmq::socket_t execution_subscriber_;
    zmq::socket_t control_publisher_;
    int server_socket_;
    int port_;
    MetricsAggregator metrics_aggregator_;
    
    std::unique_ptr<std::thread> zmq_thread_;
    std::unique_ptr<std::thread> execution_thread_;
    std::unique_ptr<std::thread> server_thread_;
    
    // Thread-safe message buffers
    std::mutex message_mutex_;
    std::vector<std::string> message_buffer_;
    std::mutex execution_mutex_;
    std::vector<std::string> execution_buffer_;
    static const size_t MAX_MESSAGES = 1000;
    static const size_t MAX_EXECUTIONS = 500;
    
    // Thread pool for client handling
    static constexpr size_t MAX_WORKER_THREADS = 2;
    static constexpr size_t MAX_PENDING_CONNECTIONS = 100;
    std::vector<std::thread> worker_threads_;
    std::queue<int> pending_clients_;
    std::mutex client_queue_mutex_;
    std::condition_variable client_queue_cv_;
    std::atomic<size_t> active_connections_{0};
    
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
    
    void execution_message_loop() {
        logger_.info("Execution message processing thread started");
        
        while (running_) {
            try {
                zmq::message_t msg;
                auto result = execution_subscriber_.recv(msg, zmq::recv_flags::dontwait);
                
                if (result) {
                    if (msg.size() == sizeof(OrderExecution)) {
                        OrderExecution execution;
                        std::memcpy(&execution, msg.data(), sizeof(OrderExecution));
                        
                        // Format as JSON for web clients
                        std::string json_exec = format_execution_as_json(execution);
                        
                        // Add to execution buffer (thread-safe)
                        {
                            std::lock_guard<std::mutex> lock(execution_mutex_);
                            execution_buffer_.push_back(json_exec);
                            
                            // Keep buffer size manageable
                            if (execution_buffer_.size() > MAX_EXECUTIONS) {
                                execution_buffer_.erase(execution_buffer_.begin());
                            }
                        }
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                
            } catch (const std::exception& e) {
                logger_.error("Execution message processing error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        
        logger_.info("Execution message processing thread stopped");
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
            
            // Check connection limit
            if (active_connections_.load() >= MAX_PENDING_CONNECTIONS) {
                logger_.warning("Connection limit reached, rejecting client");
                close(client_socket);
                continue;
            }
            
            // Queue client for worker thread handling
            {
                std::lock_guard<std::mutex> lock(client_queue_mutex_);
                pending_clients_.push(client_socket);
            }
            client_queue_cv_.notify_one();
            active_connections_++;
        }
        
        logger_.info("HTTP server thread stopped");
    }
    
    void worker_thread() {
        while (running_) {
            int client_socket = -1;
            
            // Wait for client to process
            {
                std::unique_lock<std::mutex> lock(client_queue_mutex_);
                client_queue_cv_.wait(lock, [this] { 
                    return !pending_clients_.empty() || !running_; 
                });
                
                if (!running_) break;
                
                if (!pending_clients_.empty()) {
                    client_socket = pending_clients_.front();
                    pending_clients_.pop();
                }
            }
            
            if (client_socket >= 0) {
                handle_client(client_socket);
                close(client_socket);
                active_connections_--;
            }
        }
    }
    
    void handle_client(int client_socket) {
        // Set client socket timeouts for both send and receive
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        
        char buffer[1024] = {0};
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            std::string request(buffer, bytes_read);
            
            // Parse request method and path
            std::string method, path;
            parse_request(request, method, path);
            
            // Debug logging
            logger_.info("Received HTTP request for path: " + path);
            
            std::string response;
            try {
                // Handle POST requests for control API
                if (method == "POST" && path == "/api/control/start") {
                    logger_.info("Received start control command");
                    response = handle_control_command(ControlAction::START_TRADING);
                } else if (method == "POST" && path == "/api/control/stop") {
                    logger_.info("Received stop control command");
                    response = handle_control_command(ControlAction::STOP_TRADING);
                } else if (path == "/metrics") {
                    // Prometheus metrics endpoint - all services
                    logger_.info("Building aggregated metrics response");
                    response = build_metrics_response();
                    logger_.info("Metrics response size: " + std::to_string(response.length()));
                } else if (path == "/metrics/market_data") {
                    // Market Data Handler specific metrics
                    logger_.info("Building market data metrics response");
                    response = build_service_metrics_response("MarketDataHandler");
                } else if (path == "/metrics/strategy_engine") {
                    // Strategy Engine specific metrics
                    logger_.info("Building strategy engine metrics response");
                    response = build_service_metrics_response("StrategyEngine");
                } else if (path == "/metrics/order_gateway") {
                    // Order Gateway specific metrics
                    logger_.info("Building order gateway metrics response");
                    response = build_service_metrics_response("OrderGateway");
                } else if (path == "/metrics/position_service") {
                    // Position & Risk Service specific metrics
                    logger_.info("Building position service metrics response");
                    response = build_service_metrics_response("PositionRiskService");
                } else if (path == "/api/executions") {
                    // Recent executions endpoint
                    logger_.info("Building executions response");
                    response = build_executions_response();
                } else {
                    // Default: return current messages
                    logger_.info("Building default HTTP response");
                    response = build_http_response();
                }
                
                if (!response.empty()) {
                    // Send response in chunks to handle large responses
                    const char* data = response.c_str();
                    size_t total_size = response.length();
                    size_t sent_total = 0;
                    
                    while (sent_total < total_size) {
                        ssize_t sent = send(client_socket, data + sent_total, 
                                          total_size - sent_total, MSG_NOSIGNAL);
                        if (sent <= 0) {
                            logger_.warning("Failed to send data to client, sent: " + 
                                          std::to_string(sent_total) + "/" + std::to_string(total_size));
                            break;
                        }
                        sent_total += sent;
                    }
                    
                    if (sent_total == total_size) {
                        logger_.info("Successfully sent " + std::to_string(sent_total) + " bytes to client");
                    }
                } else {
                    logger_.warning("Generated empty response for path: " + path);
                    // Send a simple error response
                    std::string error_response = "HTTP/1.1 500 Internal Server Error\r\n"
                                               "Content-Length: 0\r\n\r\n";
                    send(client_socket, error_response.c_str(), error_response.length(), MSG_NOSIGNAL);
                }
                
            } catch (const std::exception& e) {
                logger_.error("Exception in handle_client: " + std::string(e.what()));
                std::string error_response = "HTTP/1.1 500 Internal Server Error\r\n"
                                           "Content-Length: 0\r\n\r\n";
                send(client_socket, error_response.c_str(), error_response.length(), MSG_NOSIGNAL);
            }
            
        } else {
            logger_.warning("No bytes read from client socket, errno: " + std::to_string(errno));
        }
    }
    
    void parse_request(const std::string& request, std::string& method, std::string& path) {
        std::istringstream stream(request);
        std::string version;
        stream >> method >> path >> version;
    }
    
    std::string build_metrics_response() {
        try {
            // Get aggregated metrics from all services
            logger_.info("Getting aggregated metrics...");
            auto aggregated_metrics = metrics_aggregator_.get_all_metrics();
            logger_.info("Got " + std::to_string(aggregated_metrics.size()) + " metrics, exporting...");
            
            std::string metrics_data = PrometheusExporter::export_metrics(&aggregated_metrics);
            logger_.info("Exported metrics data, size: " + std::to_string(metrics_data.length()));
            
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: " << PrometheusExporter::get_content_type() << "\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << metrics_data.length() << "\r\n";
            response << "\r\n";
            response << metrics_data;
            
            return response.str();
            
        } catch (const std::exception& e) {
            logger_.error("Exception in build_metrics_response: " + std::string(e.what()));
            
            // Return a minimal valid response
            std::string error_data = "# Error generating metrics\n";
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: text/plain\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << error_data.length() << "\r\n";
            response << "\r\n";
            response << error_data;
            
            return response.str();
        }
    }
    
    std::string build_service_metrics_response(const std::string& service_name) {
        try {
            // Get metrics for specific service
            logger_.info("Getting metrics for service: " + service_name);
            auto service_metrics = metrics_aggregator_.get_service_metrics(service_name);
            logger_.info("Got " + std::to_string(service_metrics.size()) + " metrics for " + service_name + ", exporting...");
            
            std::string metrics_data = PrometheusExporter::export_metrics(&service_metrics);
            logger_.info("Exported service metrics data, size: " + std::to_string(metrics_data.length()));
            
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: " << PrometheusExporter::get_content_type() << "\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << metrics_data.length() << "\r\n";
            response << "\r\n";
            response << metrics_data;
            
            return response.str();
            
        } catch (const std::exception& e) {
            logger_.error("Exception in build_service_metrics_response for " + service_name + ": " + std::string(e.what()));
            
            // Return a minimal valid response
            std::string error_data = "# Error generating metrics for " + service_name + "\n";
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\r\n";
            response << "Content-Type: text/plain\r\n";
            response << "Access-Control-Allow-Origin: *\r\n";
            response << "Content-Length: " << error_data.length() << "\r\n";
            response << "\r\n";
            response << error_data;
            
            return response.str();
        }
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
    
    std::string build_executions_response() {
        // Get current executions
        std::vector<std::string> executions;
        {
            std::lock_guard<std::mutex> lock(execution_mutex_);
            executions = execution_buffer_;
        }
        
        // Build JSON response
        std::ostringstream json;
        json << "{\"executions\":[";
        
        for (size_t i = 0; i < executions.size(); ++i) {
            if (i > 0) json << ",";
            json << executions[i];
        }
        
        json << "],\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() << "}";
        
        std::string body = json.str();
        
        // Build HTTP response
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\\r\\n";
        response << "Content-Type: application/json\\r\\n";
        response << "Access-Control-Allow-Origin: *\\r\\n";
        response << "Content-Length: " << body.length() << "\\r\\n";
        response << "\\r\\n";
        response << body;
        
        return response.str();
    }
    
    std::string handle_control_command(ControlAction action) {
        try {
            // Create control command message
            ControlCommand command{};
            command.header = MessageFactory::create_header(MessageType::CONTROL_COMMAND, 
                                                         sizeof(ControlCommand) - sizeof(MessageHeader));
            command.action = action;
            std::strncpy(command.target_service, "MarketDataHandler", sizeof(command.target_service) - 1);
            std::strncpy(command.parameters, "{}", sizeof(command.parameters) - 1);
            
            // Publish control command
            zmq::message_t message(sizeof(ControlCommand));
            std::memcpy(message.data(), &command, sizeof(ControlCommand));
            control_publisher_.send(message, zmq::send_flags::dontwait);
            
            logger_.info("Control command sent: " + std::to_string(static_cast<int>(action)));
            
            // Build success response
            std::string body = "{\"success\":true,\"message\":\"Command executed\"}";
            
            std::ostringstream response;
            response << "HTTP/1.1 200 OK\\r\\n";
            response << "Content-Type: application/json\\r\\n";
            response << "Access-Control-Allow-Origin: *\\r\\n";
            response << "Content-Length: " << body.length() << "\\r\\n";
            response << "\\r\\n";
            response << body;
            
            return response.str();
            
        } catch (const std::exception& e) {
            logger_.error("Control command failed: " + std::string(e.what()));
            
            std::string body = "{\"success\":false,\"message\":\"" + std::string(e.what()) + "\"}";
            
            std::ostringstream response;
            response << "HTTP/1.1 500 Internal Server Error\\r\\n";
            response << "Content-Type: application/json\\r\\n";
            response << "Access-Control-Allow-Origin: *\\r\\n";
            response << "Content-Length: " << body.length() << "\\r\\n";
            response << "\\r\\n";
            response << body;
            
            return response.str();
        }
    }
    
    std::string format_execution_as_json(const OrderExecution& execution) {
        // Convert execution types to strings
        std::string exec_type_str;
        switch (execution.exec_type) {
            case ExecutionType::NEW: exec_type_str = "NEW"; break;
            case ExecutionType::PARTIAL_FILL: exec_type_str = "PARTIAL"; break;
            case ExecutionType::FILL: exec_type_str = "FILL"; break;
            case ExecutionType::CANCELLED: exec_type_str = "CANCELLED"; break;
            case ExecutionType::REJECTED: exec_type_str = "REJECTED"; break;
            default: exec_type_str = "UNKNOWN"; break;
        }
        
        // Get current timestamp for display
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        std::ostringstream json;
        json << "{";
        json << "\"order_id\":" << execution.order_id << ",";
        json << "\"symbol\":\"" << std::string(execution.symbol) << "\",";
        json << "\"type\":\"" << exec_type_str << "\",";
        json << "\"action\":\"" << (execution.fill_quantity > 0 ? "BUY" : "SELL") << "\",";
        json << "\"quantity\":" << execution.fill_quantity << ",";
        json << "\"price\":" << std::fixed << std::setprecision(2) << execution.fill_price << ",";
        json << "\"commission\":" << std::fixed << std::setprecision(4) << execution.commission << ",";
        json << "\"timestamp\":\"" << std::setfill('0') << std::setw(2) << tm.tm_hour << ":"
             << std::setfill('0') << std::setw(2) << tm.tm_min << ":"
             << std::setfill('0') << std::setw(2) << tm.tm_sec << "\"";
        json << "}";
        
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