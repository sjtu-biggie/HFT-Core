#pragma once

#include <cstdint>
#include <chrono>
#include <string>

#ifdef __x86_64__
#include <x86intrin.h>
#endif

namespace hft {

// High-precision timer using RDTSC instruction for sub-nanosecond timing
// Critical for measuring HFT latencies where every nanosecond counts
class HighResTimer {
public:
    using ticks_t = uint64_t;
    
    // Initialize timer and calibrate TSC frequency
    static void initialize();
    
    // Get current timestamp in CPU ticks (highest precision)
    static inline ticks_t get_ticks() {
#ifdef __x86_64__
        return __rdtsc();
#else
        // Fallback for non-x86 architectures
        return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
    }
    
    // Get current timestamp in nanoseconds (calibrated)
    static inline uint64_t get_nanoseconds() {
        return ticks_to_nanoseconds(get_ticks());
    }
    
    // Convert ticks to nanoseconds using calibrated frequency
    static inline uint64_t ticks_to_nanoseconds(ticks_t ticks) {

        return (ticks * 1000000000ULL) / tsc_frequency_;
    }
    
    // Convert nanoseconds to ticks
    static inline ticks_t nanoseconds_to_ticks(uint64_t nanoseconds) {

        return (nanoseconds * tsc_frequency_) / 1000000000ULL;
    }
    
    // Get TSC frequency in Hz
    static uint64_t get_tsc_frequency() { return tsc_frequency_; }
    
    // Check if high-precision timing is available
    static bool is_high_precision_available() { 
#ifdef __x86_64__
        return tsc_frequency_ > 0;
#else
        return false;
#endif
    }
    
    // Calibration info
    static std::string get_timer_info();

private:
    static uint64_t tsc_frequency_;  // TSC frequency in Hz
    static bool initialized_;
    
    // Calibrate TSC frequency against std::chrono
    static void calibrate_tsc_frequency();
};

// RAII timer for measuring elapsed time with minimal overhead
class ScopedTimer {
public:
    explicit ScopedTimer(const char* name = "Timer") 
        : name_(name), start_ticks_(HighResTimer::get_ticks()) {}
    
    ~ScopedTimer() {
        uint64_t elapsed_ns = HighResTimer::ticks_to_nanoseconds(
            HighResTimer::get_ticks() - start_ticks_);
        // Could log to metrics here, but for now just store
        elapsed_nanoseconds_ = elapsed_ns;
    }
    
    // Get elapsed time without destroying the timer
    uint64_t get_elapsed_nanoseconds() const {
        return HighResTimer::ticks_to_nanoseconds(
            HighResTimer::get_ticks() - start_ticks_);
    }
    
    uint64_t get_elapsed_ticks() const {
        return HighResTimer::get_ticks() - start_ticks_;
    }
    
    const char* get_name() const { return name_; }
    uint64_t get_final_elapsed_ns() const { return elapsed_nanoseconds_; }

private:
    const char* name_;
    HighResTimer::ticks_t start_ticks_;
    uint64_t elapsed_nanoseconds_ = 0;
};

// Macro for easy timer creation with automatic naming
#define HFT_TIMER() hft::ScopedTimer _timer(__FUNCTION__)
#define HFT_TIMER_NAMED(name) hft::ScopedTimer _timer(name)

// Lightweight timing point for strategic placement
struct TimingPoint {
    HighResTimer::ticks_t timestamp;
    const char* label;
    
    TimingPoint(const char* lbl) : timestamp(HighResTimer::get_ticks()), label(lbl) {}
    
    // Calculate nanoseconds since this timing point
    uint64_t nanoseconds_since() const {
        return HighResTimer::ticks_to_nanoseconds(
            HighResTimer::get_ticks() - timestamp);
    }
    
    // Calculate nanoseconds between two timing points
    static uint64_t nanoseconds_between(const TimingPoint& start, const TimingPoint& end) {
        return HighResTimer::ticks_to_nanoseconds(end.timestamp - start.timestamp);
    }
};

// Critical path timing - for the most performance-sensitive code paths
// This class is designed for zero allocation, minimal overhead timing
class CriticalPathTimer {
public:
    explicit CriticalPathTimer() : start_ticks_(HighResTimer::get_ticks()) {}
    
    // Get elapsed nanoseconds (inline for minimal overhead)
    inline uint64_t get_elapsed_ns() const {
        return HighResTimer::ticks_to_nanoseconds(
            HighResTimer::get_ticks() - start_ticks_);
    }
    
    // Get elapsed ticks (fastest possible timing)
    inline HighResTimer::ticks_t get_elapsed_ticks() const {
        return HighResTimer::get_ticks() - start_ticks_;
    }
    
    // Reset timer
    inline void reset() {
        start_ticks_ = HighResTimer::get_ticks();
    }

private:
    HighResTimer::ticks_t start_ticks_;
};

} // namespace hft