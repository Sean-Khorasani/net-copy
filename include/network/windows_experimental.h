#pragma once

#include "network/socket.h"
#include <cstdint>
#include <string>

namespace netcopy {
namespace network {
namespace windows_experimental {

struct FeatureSupport {
    bool rio_compiled = false;
    bool rio_available = false;
    bool ioring_compiled = false;
    bool ioring_available = false;
    bool msquic_compiled = false;
};

FeatureSupport probe_features(socket_t socket);
uint64_t recommended_tcp_inflight_window(socket_t socket, uint64_t fallback_bytes);
std::string feature_summary(const FeatureSupport& support);

} // namespace windows_experimental
} // namespace network
} // namespace netcopy
