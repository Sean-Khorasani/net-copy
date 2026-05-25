#include "common/chunk_size_manager.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace netcopy {
namespace common {

ChunkSizeManager::ChunkSizeManager(size_t initial_chunk_size, 
                                 size_t min_chunk_size, 
                                 size_t max_chunk_size,
                                 double increase_factor,
                                 double decrease_factor,
                                 double ema_alpha)
    : initial_chunk_size_(initial_chunk_size),
      min_chunk_size_(min_chunk_size),
      max_chunk_size_(max_chunk_size),
      increase_factor_(increase_factor),
      decrease_factor_(decrease_factor),
      ema_alpha_(ema_alpha),
      current_chunk_size_(initial_chunk_size),
      smoothed_throughput_(0.0),
      last_update_time_(std::chrono::steady_clock::now()) {
    normalize_limits();
}

size_t ChunkSizeManager::get_optimal_chunk_size(const BandwidthMonitor& monitor) {
    std::lock_guard<std::mutex> lock(mutex_);
    // For now, just return the current chunk size
    // In future implementations, we might adjust based on bandwidth measurements
    return current_chunk_size_;
}

void ChunkSizeManager::update_chunk_size(const BandwidthMonitor& monitor, 
                                       bool success, 
                                       uint64_t bytes_transferred) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Update based on success/failure and throughput
    adjust_chunk_size_based_on_success(monitor, success, bytes_transferred);
    
    // For more advanced adaptive behavior, we could also consider bandwidth
    // adjust_chunk_size_based_on_throughput(monitor);
}

void ChunkSizeManager::set_adaptation_parameters(double increase_factor, double decrease_factor, double ema_alpha) {
    std::lock_guard<std::mutex> lock(mutex_);
    increase_factor_ = std::max(1.01, increase_factor);
    decrease_factor_ = std::min(0.99, std::max(0.1, decrease_factor));
    ema_alpha_ = std::min(1.0, std::max(0.01, ema_alpha));
}

void ChunkSizeManager::set_limits(size_t initial_chunk_size, size_t min_chunk_size, size_t max_chunk_size) {
    initial_chunk_size_ = initial_chunk_size;
    min_chunk_size_ = min_chunk_size;
    max_chunk_size_ = max_chunk_size;
    normalize_limits();
    reset();
}

void ChunkSizeManager::set_max_chunk_size(size_t max_chunk_size) {
    max_chunk_size_ = max_chunk_size;
    normalize_limits();
    current_chunk_size_ = std::min(current_chunk_size_, max_chunk_size_);
}

void ChunkSizeManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_chunk_size_ = initial_chunk_size_;
    smoothed_throughput_ = 0.0;
    throughput_history_.clear();
    last_update_time_ = std::chrono::steady_clock::now();
}

void ChunkSizeManager::adjust_chunk_size_based_on_success(const BandwidthMonitor& monitor,
                                                         bool success,
                                                         uint64_t bytes_transferred) {
    auto now = std::chrono::steady_clock::now();
    
    if (!success) {
        if (current_chunk_size_ > min_chunk_size_) {
            size_t new_size = static_cast<size_t>(current_chunk_size_ * decrease_factor_);
            current_chunk_size_ = std::max(new_size, min_chunk_size_);
        }
        // Reset measurement window
        throughput_history_.clear();
        throughput_history_.push_back({0, now});
        return;
    }
    
    // Add to throughput history
    throughput_history_.push_back({bytes_transferred, now});
    
    auto time_span_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        throughput_history_.back().second - throughput_history_.front().second).count();
        
    // Wait until we have at least 50ms of data to calculate stable throughput
    if (time_span_ms < 50) {
        return;
    }
    
    // Calculate throughput over the window
    uint64_t total_bytes = 0;
    for (const auto& entry : throughput_history_) {
        total_bytes += entry.first;
    }
    // The time span is from the END of the first chunk to the END of the last chunk.
    // So the bytes transferred during this time span is total_bytes minus the first chunk.
    total_bytes -= throughput_history_.front().first;
    
    double current_throughput = static_cast<double>(total_bytes) / (time_span_ms / 1000.0);
    double previous_throughput = smoothed_throughput_;

    // Update smoothed throughput using EMA
    if (smoothed_throughput_ == 0.0) {
        smoothed_throughput_ = current_throughput;
    } else {
        smoothed_throughput_ = (ema_alpha_ * current_throughput) + ((1.0 - ema_alpha_) * smoothed_throughput_);
    }
    
    // Start a new measurement window from now
    throughput_history_.clear();
    throughput_history_.push_back({0, now});
    
    // Bandwidth-Delay Product (BDP) Proportional Controller:
    // Target chunk transfer time is 50 milliseconds. 
    // This provides smooth progress bars and responsive transfers while scaling flawlessly to any bandwidth.
    constexpr double TARGET_CHUNK_TIME_SEC = 0.05;
    
    size_t ideal_chunk_size = static_cast<size_t>(smoothed_throughput_ * TARGET_CHUNK_TIME_SEC);
    
    // Apply a mild EMA to the chunk size itself to prevent micro-oscillations
    current_chunk_size_ = static_cast<size_t>(
        (0.7 * static_cast<double>(current_chunk_size_)) + (0.3 * static_cast<double>(ideal_chunk_size))
    );
    
    // Clamp to negotiated limits
    current_chunk_size_ = std::clamp(current_chunk_size_, min_chunk_size_, max_chunk_size_);
}

void ChunkSizeManager::adjust_chunk_size_based_on_throughput(const BandwidthMonitor& monitor) {
    // This would be used for more advanced bandwidth-based adjustments
    // For now, we're using success/failure based approach which is simpler and more reliable
}

double ChunkSizeManager::calculate_average_throughput() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (throughput_history_.empty()) {
        return 0.0;
    }
    
    uint64_t total_bytes = 0;
    for (const auto& entry : throughput_history_) {
        total_bytes += entry.first;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto total_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - throughput_history_.front().second).count();
    
    if (total_time_ms == 0) {
        return 0.0;
    }
    
    // Convert to bytes per second
    double bytes_per_second = static_cast<double>(total_bytes) / 
                             (total_time_ms / 1000.0);
    
    return bytes_per_second;
}

void ChunkSizeManager::normalize_limits() {
    // Caller must hold mutex_.
    static constexpr size_t ABSOLUTE_MIN_CHUNK_SIZE = 1024;
    min_chunk_size_ = std::max(min_chunk_size_, ABSOLUTE_MIN_CHUNK_SIZE);
    max_chunk_size_ = std::max(max_chunk_size_, min_chunk_size_);
    initial_chunk_size_ = std::min(std::max(initial_chunk_size_, min_chunk_size_), max_chunk_size_);
    current_chunk_size_ = std::min(std::max(current_chunk_size_, min_chunk_size_), max_chunk_size_);
}

} // namespace common
} // namespace netcopy
