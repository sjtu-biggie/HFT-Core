#include "high_res_timer.h"
#include <iostream>
#include <thread>
#include <sstream>

namespace hft {

// Static member definitions
uint64_t HighResTimer::tsc_frequency_ = 0;
bool HighResTimer::initialized_ = false;

void HighResTimer::initialize() {
    if (initialized_) {
        return;
    }
    
    std::cout << "[HighResTimer] Initializing high-precision timer..." << std::endl;
    
#ifdef __x86_64__
    calibrate_tsc_frequency();
    
    if (tsc_frequency_ > 0) {
        std::cout << "[HighResTimer] TSC frequency: " << tsc_frequency_ << " Hz" << std::endl;
        std::cout << "[HighResTimer] Timer resolution: " << 
                    (1000000000.0 / tsc_frequency_) << " ns per tick" << std::endl;
        std::cout << "[HighResTimer] High-precision timing enabled" << std::endl;
    } else {
        std::cout << "[HighResTimer] Warning: TSC calibration failed, falling back to std::chrono" << std::endl;
    }
#else
    std::cout << "[HighResTimer] Warning: RDTSC not available on this architecture, using std::chrono" << std::endl;
#endif
    
    initialized_ = true;
}

void HighResTimer::calibrate_tsc_frequency() {
#ifdef __x86_64__
    std::cout << "[HighResTimer] Calibrating TSC frequency..." << std::endl;
    
    // Use multiple calibration runs for better accuracy
    const int num_runs = 5;
    const auto calibration_duration = std::chrono::milliseconds(100);
    uint64_t total_frequency = 0;
    int valid_runs = 0;
    
    for (int run = 0; run < num_runs; ++run) {
        // Get initial timestamps
        auto chrono_start = std::chrono::steady_clock::now();
        uint64_t tsc_start = __rdtsc();
        
        // Wait for calibration period
        std::this_thread::sleep_for(calibration_duration);
        
        // Get final timestamps
        auto chrono_end = std::chrono::steady_clock::now();
        uint64_t tsc_end = __rdtsc();
        
        // Calculate frequencies
        auto chrono_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            chrono_end - chrono_start).count();
        uint64_t tsc_ticks = tsc_end - tsc_start;
        
        // Calculate TSC frequency
        if (chrono_duration_ns > 0 && tsc_ticks > 0) {
            uint64_t frequency = (tsc_ticks * 1000000000ULL) / chrono_duration_ns;
            total_frequency += frequency;
            valid_runs++;
            
            std::cout << "[HighResTimer] Calibration run " << (run + 1) << ": " 
                     << frequency << " Hz" << std::endl;
        }
    }
    
    if (valid_runs > 0) {
        tsc_frequency_ = total_frequency / valid_runs;
        std::cout << "[HighResTimer] Average TSC frequency: " << tsc_frequency_ << " Hz" << std::endl;
    } else {
        std::cout << "[HighResTimer] Error: All calibration runs failed" << std::endl;
        tsc_frequency_ = 0;
    }
#endif
}

std::string HighResTimer::get_timer_info() {
    std::ostringstream oss;
    
    oss << "HighResTimer Info:\n";
    oss << "  Architecture: ";
#ifdef __x86_64__
    oss << "x86_64 (RDTSC available)\n";
#else
    oss << "Non-x86 (using std::chrono)\n";
#endif
    
    oss << "  Initialized: " << (initialized_ ? "Yes" : "No") << "\n";
    
    if (initialized_ && tsc_frequency_ > 0) {
        oss << "  TSC Frequency: " << tsc_frequency_ << " Hz\n";
        oss << "  Resolution: " << (1000000000.0 / tsc_frequency_) << " ns per tick\n";
        oss << "  High Precision: Available\n";
        
        // Test timing accuracy
        auto start = get_ticks();
        std::this_thread::sleep_for(std::chrono::microseconds(1));
        auto end = get_ticks();
        
        oss << "  Test 1μs delay: " << ticks_to_nanoseconds(end - start) << " ns measured\n";
    } else {
        oss << "  High Precision: Unavailable (fallback to std::chrono)\n";
        oss << "  Resolution: ~" << 
            std::chrono::steady_clock::period::num * 1000000000LL / 
            std::chrono::steady_clock::period::den << " ns\n";
    }
    
    return oss.str();
}

} // namespace hft