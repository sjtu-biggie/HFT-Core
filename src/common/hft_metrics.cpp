#include "hft_metrics.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

#ifdef __linux__
#include <unistd.h>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <fstream>
#include <dirent.h>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#endif

namespace hft {

void SystemResourceMonitor::update_memory_usage() {
#ifdef __linux__
    std::ifstream status_file("/proc/self/status");
    std::string line;
    
    while (std::getline(status_file, line)) {
        if (line.find("VmRSS:") == 0) {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            uint64_t rss_kb = std::stoull(value);
            MetricsCollector::instance().set_gauge(metrics::MEMORY_RSS, rss_kb / 1024); // Convert to MB
        } else if (line.find("VmSize:") == 0) {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            uint64_t vms_kb = std::stoull(value);
            MetricsCollector::instance().set_gauge(metrics::MEMORY_VMS, vms_kb / 1024); // Convert to MB
        }
    }
    
    // Get heap info from mallinfo if available
    #ifdef __GLIBC__
    struct mallinfo mi = mallinfo();
    MetricsCollector::instance().set_gauge(metrics::MEMORY_HEAP, mi.uordblks / (1024 * 1024)); // Convert to MB
    #endif
    
#endif
}

void SystemResourceMonitor::update_cpu_usage() {
#ifdef __linux__
    static uint64_t last_total = 0, last_idle = 0;
    
    std::ifstream stat_file("/proc/stat");
    std::string line;
    
    if (std::getline(stat_file, line)) {
        std::istringstream iss(line);
        std::string cpu;
        uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
        
        iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        
        uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
        uint64_t total_diff = total - last_total;
        uint64_t idle_diff = idle - last_idle;
        
        if (total_diff > 0) {
            uint64_t cpu_usage = ((total_diff - idle_diff) * 100) / total_diff;
            MetricsCollector::instance().set_gauge(metrics::CPU_USAGE, cpu_usage);
        }
        
        last_total = total;
        last_idle = idle;
    }
    
    // Get context switches
    std::ifstream vmstat_file("/proc/vmstat");
    while (std::getline(vmstat_file, line)) {
        if (line.find("nr_context_switches") == 0) {
            std::istringstream iss(line);
            std::string key;
            uint64_t value;
            iss >> key >> value;
            MetricsCollector::instance().set_gauge(metrics::CPU_CONTEXT_SWITCHES, value);
            break;
        }
    }
#endif
}

void SystemResourceMonitor::update_network_stats() {
#ifdef __linux__
    std::ifstream net_file("/proc/net/dev");
    std::string line;
    
    // Skip header lines
    std::getline(net_file, line);
    std::getline(net_file, line);
    
    uint64_t total_bytes_recv = 0, total_bytes_sent = 0;
    uint64_t total_packets_recv = 0, total_packets_sent = 0;
    uint64_t total_errors = 0, total_drops = 0;
    
    while (std::getline(net_file, line)) {
        std::istringstream iss(line);
        std::string interface;
        uint64_t bytes_recv, packets_recv, errs_recv, drop_recv, fifo_recv, frame_recv, compressed_recv, multicast_recv;
        uint64_t bytes_sent, packets_sent, errs_sent, drop_sent, fifo_sent, colls_sent, carrier_sent, compressed_sent;
        
        iss >> interface;
        // Remove colon from interface name
        if (!interface.empty() && interface.back() == ':') {
            interface.pop_back();
        }
        
        // Skip loopback interface
        if (interface == "lo") continue;
        
        iss >> bytes_recv >> packets_recv >> errs_recv >> drop_recv >> fifo_recv >> frame_recv >> compressed_recv >> multicast_recv
            >> bytes_sent >> packets_sent >> errs_sent >> drop_sent >> fifo_sent >> colls_sent >> carrier_sent >> compressed_sent;
        
        total_bytes_recv += bytes_recv;
        total_bytes_sent += bytes_sent;
        total_packets_recv += packets_recv;
        total_packets_sent += packets_sent;
        total_errors += errs_recv + errs_sent;
        total_drops += drop_recv + drop_sent;
    }
    
    MetricsCollector::instance().set_gauge(metrics::NETWORK_BYTES_RECV, total_bytes_recv);
    MetricsCollector::instance().set_gauge(metrics::NETWORK_BYTES_SENT, total_bytes_sent);
    MetricsCollector::instance().set_gauge(metrics::NETWORK_PACKETS_RECV, total_packets_recv);
    MetricsCollector::instance().set_gauge(metrics::NETWORK_PACKETS_SENT, total_packets_sent);
    MetricsCollector::instance().set_gauge(metrics::NETWORK_ERRORS, total_errors);
    MetricsCollector::instance().set_gauge(metrics::NETWORK_DROPS, total_drops);
#endif
}

void SystemResourceMonitor::update_thread_stats() {
#ifdef __linux__
    // Count threads in /proc/self/task
    std::string task_dir = "/proc/self/task";
    DIR* dir = opendir(task_dir.c_str());
    if (dir) {
        struct dirent* entry;
        uint64_t thread_count = 0;
        
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_name[0] != '.') { // Skip . and ..
                thread_count++;
            }
        }
        closedir(dir);
        
        MetricsCollector::instance().set_gauge(metrics::THREAD_COUNT, thread_count);
    }
#else
    // Fallback for non-Linux systems
    MetricsCollector::instance().set_gauge(metrics::THREAD_COUNT, 1);
#endif
}

// Component throughput tracker implementation
ComponentThroughput::ComponentThroughput(const char* counter_name, const char* rate_name) 
    : counter_name_(counter_name), rate_name_(rate_name), last_count_(0) {
    last_timestamp_ = HighResTimer::get_nanoseconds();
}

void ComponentThroughput::increment(uint64_t count) {
    // Always increment counter
    for (uint64_t i = 0; i < count; ++i) {
        MetricsCollector::instance().increment_counter(counter_name_);
    }
    
    // Update rate gauge periodically
    uint64_t now = HighResTimer::get_nanoseconds();
    uint64_t elapsed_ns = now - last_timestamp_;
    
    if (elapsed_ns >= 1000000000ULL) { // 1 second
        last_count_ += count;
        uint64_t rate = (last_count_ * 1000000000ULL) / elapsed_ns;
        MetricsCollector::instance().set_gauge(rate_name_, rate);
        
        // Reset for next interval
        last_count_ = 0;
        last_timestamp_ = now;
    } else {
        last_count_ += count;
    }
}

// Enhanced system monitoring thread
class SystemMonitorThread {
private:
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> monitor_thread_;
    
public:
    SystemMonitorThread() : running_(false) {}
    
    void start() {
        running_ = true;
        monitor_thread_ = std::make_unique<std::thread>(&SystemMonitorThread::monitor_loop, this);
    }
    
    void stop() {
        running_ = false;
        if (monitor_thread_ && monitor_thread_->joinable()) {
            monitor_thread_->join();
        }
    }
    
private:
    void monitor_loop() {
        while (running_) {
            SystemResourceMonitor::update_memory_usage();
            SystemResourceMonitor::update_cpu_usage();
            SystemResourceMonitor::update_network_stats();
            SystemResourceMonitor::update_thread_stats();
            
            // Update system uptime
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            MetricsCollector::instance().set_gauge(metrics::SERVICE_UPTIME, uptime);
            
            // Update heartbeat
            auto heartbeat = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            MetricsCollector::instance().set_gauge(metrics::HEARTBEAT, heartbeat);
            
            // Sleep for 1 second
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

// Global system monitor instance
static SystemMonitorThread g_system_monitor;

// Initialize HFT metrics system
void initialize_hft_metrics() {
    MetricsCollector::instance().initialize();
    g_system_monitor.start();
}

// Shutdown HFT metrics system
void shutdown_hft_metrics() {
    g_system_monitor.stop();
    MetricsCollector::instance().shutdown();
}

} // namespace hft