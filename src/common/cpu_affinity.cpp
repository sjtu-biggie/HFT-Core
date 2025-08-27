#include "cpu_affinity.h"
#include "logging.h"
#include <iostream>
#include <sstream>

#ifdef __linux__
#include <sys/mman.h>
#include <sys/resource.h>
#ifdef HAS_NUMA
#include <numa.h>
#endif
#endif

namespace hft {

// CPU performance utilities implementation
class CPUPerformance {
public:
    static bool optimize_for_trading() {
        bool success = true;
        
        // Try to set real-time priority
        if (!CPUAffinity::set_realtime_priority()) {
            std::cerr << "Warning: Failed to set real-time priority. Run with sudo or adjust limits." << std::endl;
            success = false;
        }
        
        // Try to lock memory pages
        if (!CPUAffinity::lock_memory_pages()) {
            std::cerr << "Warning: Failed to lock memory pages. This may impact latency." << std::endl;
            success = false;
        }
        
        return success;
    }
    
    static std::string get_cpu_info() {
        std::ostringstream info;
        info << "CPU Count: " << CPUAffinity::get_cpu_count() << std::endl;
        info << "Thread ID: " << CPUAffinity::get_thread_id() << std::endl;
        
#ifdef __linux__
#ifdef HAS_NUMA
        // Check if NUMA is available
        if (numa_available() != -1) {
            info << "NUMA nodes: " << numa_num_configured_nodes() << std::endl;
            info << "NUMA CPUs: " << numa_num_configured_cpus() << std::endl;
        }
#else
        info << "NUMA support not compiled in" << std::endl;
#endif
#endif
        
        return info.str();
    }
    
    static bool set_cpu_governor_performance() {
#ifdef __linux__
        // This would typically require root access and system configuration
        // In production, this would be done via system startup scripts
        std::cerr << "Note: Set CPU governor to 'performance' manually for best results:" << std::endl;
        std::cerr << "  sudo cpupower frequency-set -g performance" << std::endl;
        return false;
#else
        return false;
#endif
    }
    
    // Warm up CPU caches and branch predictor
    static void warmup_cpu(int iterations = 1000000) {
        volatile int sum = 0;
        for (int i = 0; i < iterations; ++i) {
            sum += i * i;
            if (i % 1000 == 0) {
                CPUAffinity::cpu_pause();
            }
        }
        
        // Touch memory to warm up caches
        std::vector<char> warmup_buffer(1024 * 1024, 0); // 1MB
        for (size_t i = 0; i < warmup_buffer.size(); i += 64) {
            warmup_buffer[i] = static_cast<char>(i & 0xFF);
        }
    }
};

// High-performance spinlock using atomic operations and CPU pause
class SpinLock {
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    
public:
    void lock() {
        int spin_count = 0;
        while (flag_.test_and_set(std::memory_order_acquire)) {
            ++spin_count;
            if (spin_count < 16) {
                CPUAffinity::cpu_pause();
            } else if (spin_count < 32) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::nanoseconds(1));
                spin_count = 0;
            }
        }
    }
    
    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }
    
    void unlock() {
        flag_.clear(std::memory_order_release);
    }
};

// RAII lock guard for spinlock
class SpinLockGuard {
private:
    SpinLock& lock_;
    
public:
    explicit SpinLockGuard(SpinLock& lock) : lock_(lock) {
        lock_.lock();
    }
    
    ~SpinLockGuard() {
        lock_.unlock();
    }
    
    // Non-copyable
    SpinLockGuard(const SpinLockGuard&) = delete;
    SpinLockGuard& operator=(const SpinLockGuard&) = delete;
};

} // namespace hft

// Global functions for easy access
namespace hft {

void initialize_high_performance_trading() {
    std::cout << "Initializing high-performance trading environment..." << std::endl;
    
    // Print CPU information
    std::cout << CPUPerformance::get_cpu_info() << std::endl;
    
    // Optimize CPU performance
    if (CPUPerformance::optimize_for_trading()) {
        std::cout << "CPU optimizations applied successfully." << std::endl;
    } else {
        std::cout << "Warning: Some CPU optimizations failed. Check permissions." << std::endl;
    }
    
    // Warm up CPU
    std::cout << "Warming up CPU caches..." << std::endl;
    CPUPerformance::warmup_cpu();
    std::cout << "CPU warmup complete." << std::endl;
    
    // Suggest additional optimizations
    CPUPerformance::set_cpu_governor_performance();
    
    std::cout << "High-performance initialization complete." << std::endl;
}

void set_thread_for_market_data() {
    // Dedicate CPU core 0 for market data processing
    int cpu_count = CPUAffinity::get_cpu_count();
    if (cpu_count < 1) {
        std::cerr << "Error: No CPUs available" << std::endl;
        return;
    }
    
    if (CPUAffinity::set_thread_affinity(0)) {
        std::cout << "Market data thread pinned to CPU 0" << std::endl;
    } else {
        std::cout << "Warning: Failed to pin market data thread to CPU 0" << std::endl;
    }
}

void set_thread_for_trading_engine() {
    // Dedicate CPU core 1 for trading engine
    int cpu_count = CPUAffinity::get_cpu_count();
    if (cpu_count < 2) {
        std::cerr << "Warning: Less than 2 CPUs available, using CPU 0" << std::endl;
        CPUAffinity::set_thread_affinity(0);
        return;
    }
    
    if (CPUAffinity::set_thread_affinity(1)) {
        std::cout << "Trading engine thread pinned to CPU 1" << std::endl;
    } else {
        std::cout << "Warning: Failed to pin trading engine thread to CPU 1" << std::endl;
    }
}

void set_thread_for_order_gateway() {
    // Dedicate CPU core 2 for order gateway
    int cpu_count = CPUAffinity::get_cpu_count();
    int target_cpu = (cpu_count >= 3) ? 2 : (cpu_count - 1);
    
    if (cpu_count < 3) {
        std::cerr << "Warning: Less than 3 CPUs available, using CPU " << target_cpu << std::endl;
    }
    
    if (CPUAffinity::set_thread_affinity(target_cpu)) {
        std::cout << "Order gateway thread pinned to CPU " << target_cpu << std::endl;
    } else {
        std::cout << "Warning: Failed to pin order gateway thread to CPU " << target_cpu << std::endl;
    }
}

} // namespace hft