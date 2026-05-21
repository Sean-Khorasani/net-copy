#pragma once

#include <cstdint>
#include <chrono>
#include <deque>
#include <mutex>
#include "common/bandwidth_monitor.h"

namespace netcopy {
namespace common {

class ChunkSizeManager {
public:
    // Constructor with configurable parameters
    explicit ChunkSizeManager(size_t initial_chunk_size = 262144,  // 256KB default
                             size_t min_chunk_size = 8192,        // 8KB minimum
                             size_t max_chunk_size = 10485760,   // 10MB maximum
                             double increase_factor = 1.1,        // Default 10% increase
                             double decrease_factor = 0.5,        // Default 50% decrease
                             double ema_alpha = 0.3);            // Default smoothing factor
    
    // Get the optimal chunk size based on current conditions
    size_t get_optimal_chunk_size(const BandwidthMonitor& monitor);
    
    // Update the chunk size based on transfer performance
    void update_chunk_size(const BandwidthMonitor& monitor, 
                          bool success = true, 
                          uint64_t bytes_transferred = 0);
    
    // Set adaptation parameters
    void set_adaptation_parameters(double increase_factor, double decrease_factor, double ema_alpha);
    void set_limits(size_t initial_chunk_size, size_t min_chunk_size, size_t max_chunk_size);
    void set_max_chunk_size(size_t max_chunk_size);
    
    // Reset the manager to initial state
    void reset();
    
    // Get current chunk size
    size_t get_current_chunk_size() const { return current_chunk_size_; }
    
    // Get the configured minimum and maximum chunk sizes
    size_t get_min_chunk_size() const { return min_chunk_size_; }
    size_t get_max_chunk_size() const { return max_chunk_size_; }

private:
    mutable std::mutex mutex_;
    // Configuration parameters
    size_t initial_chunk_size_;
    size_t min_chunk_size_;
    size_t max_chunk_size_;
    double increase_factor_;
    double decrease_factor_;
    double ema_alpha_;
    
    // Current state
    size_t current_chunk_size_;
    double smoothed_throughput_; // Using EMA for throughput
    
    // Performance tracking
    std::chrono::steady_clock::time_point last_update_time_;
    std::deque<std::pair<uint64_t, std::chrono::steady_clock::time_point>> 
        throughput_history_;
    
    // Adaptation parameters
    static constexpr int MIN_SUCCESS_COUNT = 3;          // Minimum successful transfers before adjustment
    
    // Helper methods
    void adjust_chunk_size_based_on_throughput(const BandwidthMonitor& monitor);
    void adjust_chunk_size_based_on_success(const BandwidthMonitor& monitor, bool success, uint64_t bytes_transferred);
    double calculate_average_throughput() const;
    void normalize_limits();
};

} // namespace common
} // namespace netcopy
