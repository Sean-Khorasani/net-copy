#pragma once

#include <chrono>
#include <string>
#include <deque>
#include <cstdint>

namespace netcopy {
namespace common {

class BandwidthMonitor {
public:
    BandwidthMonitor();
    
    // Record bytes transferred
    void record_bytes(uint64_t bytes);
    
    // Get current transfer rate in bytes per second
    double get_current_rate() const;
    
    // Get human-readable transfer rate (e.g., "2.5 MB/s")
    std::string get_rate_string() const;
    
    // Get total bytes transferred
    uint64_t get_total_bytes() const;
    
    // Get transfer duration in seconds
    double get_duration() const;
    
    // Reset all statistics
    void reset();

private:
    struct TransferPoint {
        std::chrono::steady_clock::time_point timestamp;
        uint64_t bytes;
    };
    
    std::deque<TransferPoint> transfer_history_;
    uint64_t total_bytes_;
    std::chrono::steady_clock::time_point start_time_;
    
    // Clean old entries (older than 5 seconds for smoothing)
    void cleanup_old_entries();
    
    // Convert bytes to human-readable format
    static std::string format_bytes_per_second(double bytes_per_second);
};

} // namespace common
} // namespace netcopy