#include "common/bandwidth_monitor.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace netcopy {
namespace common {

BandwidthMonitor::BandwidthMonitor() : total_bytes_(0) {
    start_time_ = std::chrono::steady_clock::now();
}

void BandwidthMonitor::record_bytes(uint64_t bytes) {
    auto now = std::chrono::steady_clock::now();
    
    transfer_history_.push_back({now, bytes});
    total_bytes_ += bytes;
    
    // Clean up old entries to keep the sliding window manageable
    cleanup_old_entries();
}

double BandwidthMonitor::get_current_rate() const {
    if (transfer_history_.size() < 2) {
        return 0.0;
    }
    
    // Calculate rate over the last 2 seconds (or available data)
    auto now = std::chrono::steady_clock::now();
    auto cutoff_time = now - std::chrono::seconds(2);
    
    uint64_t bytes_in_window = 0;
    auto start_it = transfer_history_.end();
    
    // Find entries within the time window
    for (auto it = transfer_history_.rbegin(); it != transfer_history_.rend(); ++it) {
        if (it->timestamp >= cutoff_time) {
            bytes_in_window += it->bytes;
            start_it = it.base(); // Convert reverse iterator to forward iterator
        } else {
            break;
        }
    }
    
    if (start_it == transfer_history_.end() || bytes_in_window == 0) {
        return 0.0;
    }
    
    // Calculate time span
    auto time_span = now - (start_it - 1)->timestamp;
    auto seconds = std::chrono::duration<double>(time_span).count();
    
    if (seconds <= 0.0) {
        return 0.0;
    }
    
    return static_cast<double>(bytes_in_window) / seconds;
}

std::string BandwidthMonitor::get_rate_string() const {
    double rate = get_current_rate();
    return format_bytes_per_second(rate);
}

uint64_t BandwidthMonitor::get_total_bytes() const {
    return total_bytes_;
}

double BandwidthMonitor::get_duration() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now - start_time_;
    return std::chrono::duration<double>(duration).count();
}

void BandwidthMonitor::reset() {
    transfer_history_.clear();
    total_bytes_ = 0;
    start_time_ = std::chrono::steady_clock::now();
}

void BandwidthMonitor::cleanup_old_entries() {
    auto now = std::chrono::steady_clock::now();
    auto cutoff_time = now - std::chrono::seconds(5); // Keep 5 seconds of history
    
    // Remove entries older than cutoff
    transfer_history_.erase(
        std::remove_if(transfer_history_.begin(), transfer_history_.end(),
            [cutoff_time](const TransferPoint& point) {
                return point.timestamp < cutoff_time;
            }),
        transfer_history_.end()
    );
}

std::string BandwidthMonitor::format_bytes_per_second(double bytes_per_second) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    
    if (bytes_per_second < 1024.0) {
        oss << bytes_per_second << " B/s";
    } else if (bytes_per_second < 1024.0 * 1024.0) {
        oss << (bytes_per_second / 1024.0) << " KB/s";
    } else if (bytes_per_second < 1024.0 * 1024.0 * 1024.0) {
        oss << (bytes_per_second / (1024.0 * 1024.0)) << " MB/s";
    } else {
        oss << (bytes_per_second / (1024.0 * 1024.0 * 1024.0)) << " GB/s";
    }
    
    return oss.str();
}

} // namespace common
} // namespace netcopy