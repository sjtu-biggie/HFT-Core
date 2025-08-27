#pragma once

#include <thread>
#include <vector>
#include <string>
#include <atomic>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#endif

namespace hft {

class CPUAffinity {
public:
    // Set CPU affinity for current thread
    static bool set_thread_affinity(int cpu_id) {
#ifdef __linux__
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_id, &cpu_set);
        
        int result = sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
        return result == 0;
#else
        // Not supported on non-Linux systems
        return false;
#endif
    }
    
    // Set CPU affinity for specific thread
    static bool set_thread_affinity(std::thread::native_handle_type thread_handle, int cpu_id) {
#ifdef __linux__
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu_id, &cpu_set);
        
        int result = pthread_setaffinity_np(thread_handle, sizeof(cpu_set), &cpu_set);
        return result == 0;
#else
        return false;
#endif
    }
    
    // Get number of available CPU cores
    static int get_cpu_count() {
        return std::thread::hardware_concurrency();
    }
    
    // Set thread priority to real-time
    static bool set_realtime_priority(int priority = 99) {
#ifdef __linux__
        struct sched_param param;
        param.sched_priority = priority;
        
        int result = sched_setscheduler(0, SCHED_FIFO, &param);
        return result == 0;
#else
        return false;
#endif
    }
    
    // Get current thread ID
    static long get_thread_id() {
#ifdef __linux__
        return syscall(SYS_gettid);
#else
        return 0;
#endif
    }
    
    // Memory barriers for ordering
    static inline void memory_barrier() {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    
    static inline void load_acquire_barrier() {
        std::atomic_thread_fence(std::memory_order_acquire);
    }
    
    static inline void store_release_barrier() {
        std::atomic_thread_fence(std::memory_order_release);
    }
    
    // CPU pause instruction for spin loops
    static inline void cpu_pause() {
#ifdef __x86_64__
        asm volatile("pause" ::: "memory");
#elif defined(__aarch64__)
        asm volatile("yield" ::: "memory");
#else
        // Fallback for other architectures
        std::this_thread::yield();
#endif
    }
    
    // Disable address space layout randomization for deterministic performance
    static bool disable_aslr() {
#ifdef __linux__
        // This would typically be done via system configuration
        // or process startup parameters, not at runtime
        return false;
#else
        return false;
#endif
    }
    
    // Lock memory pages to prevent swapping
    static bool lock_memory_pages() {
#ifdef __linux__
        // Lock all current and future pages
        int result = mlockall(MCL_CURRENT | MCL_FUTURE);
        return result == 0;
#else
        return false;
#endif
    }
};

// RAII CPU affinity setter
class ScopedCPUAffinity {
private:
    bool success_;
    
public:
    explicit ScopedCPUAffinity(int cpu_id) : success_(false) {
        success_ = CPUAffinity::set_thread_affinity(cpu_id);
    }
    
    ~ScopedCPUAffinity() {
        // Restore to default affinity (all CPUs)
#ifdef __linux__
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        for (int i = 0; i < CPUAffinity::get_cpu_count(); ++i) {
            CPU_SET(i, &cpu_set);
        }
        sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
#endif
    }
    
    bool success() const { return success_; }
};

// Cache-friendly atomic counter with padding to avoid false sharing
template<typename T>
class alignas(64) PaddedAtomic {
    static_assert(sizeof(std::atomic<T>) <= 64, "PaddedAtomic: atomic type too large for 64-byte padding");
    
private:
    std::atomic<T> value_;
    char padding_[64 - sizeof(std::atomic<T>)];
    
public:
    PaddedAtomic() : value_(T{}) {}
    explicit PaddedAtomic(T initial) : value_(initial) {}
    
    T load(std::memory_order order = std::memory_order_seq_cst) const {
        return value_.load(order);
    }
    
    void store(T desired, std::memory_order order = std::memory_order_seq_cst) {
        value_.store(desired, order);
    }
    
    T exchange(T desired, std::memory_order order = std::memory_order_seq_cst) {
        return value_.exchange(desired, order);
    }
    
    bool compare_exchange_weak(T& expected, T desired,
                              std::memory_order success = std::memory_order_seq_cst,
                              std::memory_order failure = std::memory_order_seq_cst) {
        return value_.compare_exchange_weak(expected, desired, success, failure);
    }
    
    bool compare_exchange_strong(T& expected, T desired,
                                std::memory_order success = std::memory_order_seq_cst,
                                std::memory_order failure = std::memory_order_seq_cst) {
        return value_.compare_exchange_strong(expected, desired, success, failure);
    }
    
    T fetch_add(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value_.fetch_add(arg, order);
    }
    
    T fetch_sub(T arg, std::memory_order order = std::memory_order_seq_cst) {
        return value_.fetch_sub(arg, order);
    }
    
    // Prefix increment
    T operator++() {
        return fetch_add(1) + 1;
    }
    
    // Postfix increment
    T operator++(int) {
        return fetch_add(1);
    }
    
    // Prefix decrement
    T operator--() {
        return fetch_sub(1) - 1;
    }
    
    // Postfix decrement
    T operator--(int) {
        return fetch_sub(1);
    }
    
    operator T() const {
        return load();
    }
    
    T operator=(T desired) {
        store(desired);
        return desired;
    }
};

// Lock-free SPSC (Single Producer Single Consumer) queue optimized for performance
template<typename T, size_t SIZE>
class alignas(64) SPSCQueue {
private:
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be power of 2");
    
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    
    struct alignas(64) Slot {
        std::atomic<bool> ready{false};
        T data;
    };
    
    std::array<Slot, SIZE> buffer_;
    
    static constexpr size_t MASK = SIZE - 1;
    
public:
    SPSCQueue() = default;
    
    // Non-copyable, non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;
    
    // Producer side - enqueue
    bool try_enqueue(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[head & MASK];
        
        if (slot.ready.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        slot.data = item;
        slot.ready.store(true, std::memory_order_release);
        head_.store(head + 1, std::memory_order_release);
        
        return true;
    }
    
    // Consumer side - dequeue
    bool try_dequeue(T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        Slot& slot = buffer_[tail & MASK];
        
        if (!slot.ready.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        
        item = slot.data;
        slot.ready.store(false, std::memory_order_release);
        tail_.store(tail + 1, std::memory_order_release);
        
        return true;
    }
    
    // Get approximate size
    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }
    
    bool empty() const {
        return size() == 0;
    }
    
    bool full() const {
        return size() >= SIZE;
    }
};

} // namespace hft

// Global functions for easy access
namespace hft {
    void initialize_high_performance_trading();
    void set_thread_for_market_data();
    void set_thread_for_trading_engine();
    void set_thread_for_order_gateway();
}