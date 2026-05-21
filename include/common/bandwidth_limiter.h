#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace netcopy {
namespace common {

class BandwidthLimiter {
public:
    BandwidthLimiter();

    void set_limit_bytes_per_second(uint64_t bytes_per_second);
    void set_limit_percent(int percent);
    void throttle(uint64_t bytes);

    bool is_enabled() const;
    uint64_t get_limit_bytes_per_second() const;

private:
    mutable std::mutex mutex_;
    uint64_t limit_bytes_per_second_;
    std::chrono::steady_clock::time_point next_available_time_;
};

} // namespace common
} // namespace netcopy
