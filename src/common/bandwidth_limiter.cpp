#include "common/bandwidth_limiter.h"
#include "common/utils.h"
#include <algorithm>
#include <thread>

namespace netcopy {
namespace common {

BandwidthLimiter::BandwidthLimiter()
    : limit_bytes_per_second_(0),
      next_available_time_(std::chrono::steady_clock::now()) {}

void BandwidthLimiter::set_limit_bytes_per_second(uint64_t bytes_per_second) {
    std::lock_guard<std::mutex> lock(mutex_);
    limit_bytes_per_second_ = bytes_per_second;
    next_available_time_ = std::chrono::steady_clock::now();
}

void BandwidthLimiter::set_limit_percent(int percent) {
    if (percent <= 0 || percent >= 100) {
        set_limit_bytes_per_second(0);
        return;
    }

    uint64_t baseline = get_network_bandwidth();
    uint64_t limit = baseline * static_cast<uint64_t>(percent) / 100;
    set_limit_bytes_per_second((std::max)(limit, static_cast<uint64_t>(1)));
}

void BandwidthLimiter::throttle(uint64_t bytes) {
    if (bytes == 0) {
        return;
    }

    std::chrono::steady_clock::time_point sleep_until;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (limit_bytes_per_second_ == 0) {
            return;
        }

        auto now = std::chrono::steady_clock::now();
        if (next_available_time_ < now) {
            next_available_time_ = now;
        }

        sleep_until = next_available_time_;
        auto delay = std::chrono::duration<double>(
            static_cast<double>(bytes) / static_cast<double>(limit_bytes_per_second_));
        next_available_time_ += std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay);
    }

    auto now = std::chrono::steady_clock::now();
    if (sleep_until > now) {
        std::this_thread::sleep_until(sleep_until);
    }
}

bool BandwidthLimiter::is_enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return limit_bytes_per_second_ > 0;
}

uint64_t BandwidthLimiter::get_limit_bytes_per_second() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return limit_bytes_per_second_;
}

} // namespace common
} // namespace netcopy
