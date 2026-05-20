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
    auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_update_time_).count();
    
    // Calculate current throughput for this chunk
    double current_throughput = 0.0;
    if (time_elapsed > 0 && bytes_transferred > 0) {
        current_throughput = static_cast<double>(bytes_transferred) / (time_elapsed / 1000.0);
    }

    double previous_throughput = smoothed_throughput_;

    // Update smoothed throughput using EMA
    if (smoothed_throughput_ == 0.0) {
        smoothed_throughput_ = current_throughput;
    } else {
        smoothed_throughput_ = (ema_alpha_ * current_throughput) + ((1.0 - ema_alpha_) * smoothed_throughput_);
    }

    // Add to throughput history
    throughput_history_.push_back({bytes_transferred, now});
    
    // Keep only recent history (last 10 transfers)
    if (throughput_history_.size() > 10) {
        throughput_history_.pop_front();
    }
    
    // Update time
    last_update_time_ = now;
    
    // Adjust chunk size using bounded AIMD. ACKed chunks grow cautiously while
    // failed or materially slower chunks back off quickly.
    if (success && bytes_transferred > 0) {
        if (throughput_history_.size() >= MIN_SUCCESS_COUNT) {
            bool throughput_healthy = previous_throughput <= 0.0 ||
                current_throughput <= 0.0 ||
                current_throughput >= previous_throughput * 0.95;
            if (throughput_healthy && current_chunk_size_ < max_chunk_size_) {
                size_t new_size = static_cast<size_t>(current_chunk_size_ * increase_factor_);
                current_chunk_size_ = std::min(new_size, max_chunk_size_);
            } else if (current_throughput > 0.0 &&
                       current_throughput < previous_throughput * 0.70 &&
                       current_chunk_size_ > min_chunk_size_) {
                size_t new_size = static_cast<size_t>(current_chunk_size_ * decrease_factor_);
                current_chunk_size_ = std::max(new_size, min_chunk_size_);
            }
        }
    } else {
        if (current_chunk_size_ > min_chunk_size_) {
            size_t new_size = static_cast<size_t>(current_chunk_size_ * decrease_factor_);
            current_chunk_size_ = std::max(new_size, min_chunk_size_);
        }
    }
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
