#include "network/windows_experimental.h"
#include <algorithm>
#include <sstream>

#if defined(WITHIORING) && !defined(NETCOPY_WITH_IORING)
#define NETCOPY_WITH_IORING
#endif
#if defined(WITHRIO) && !defined(NETCOPY_WITH_RIO)
#define NETCOPY_WITH_RIO
#endif
#if defined(WITHMSQUIC) && !defined(NETCOPY_WITH_MSQUIC)
#define NETCOPY_WITH_MSQUIC
#endif
#if defined(WITHTCPINFO) && !defined(NETCOPY_WITH_TCP_INFO_WINDOW)
#define NETCOPY_WITH_TCP_INFO_WINDOW
#endif

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mswsock.h>
#if defined(NETCOPY_WITH_IORING) && __has_include(<ioringapi.h>)
#include <ioringapi.h>
#define NETCOPY_HAS_IORING_HEADER 1
#endif
#endif

namespace netcopy {
namespace network {
namespace windows_experimental {

namespace {
uint64_t clamp_window(uint64_t value, uint64_t fallback) {
    constexpr uint64_t min_window = 4ull * 1024ull * 1024ull;
    constexpr uint64_t max_window = 512ull * 1024ull * 1024ull;
    if (value == 0) {
        value = fallback;
    }
    return (std::max)(min_window, (std::min)(value, max_window));
}
}

FeatureSupport probe_features(socket_t socket) {
    FeatureSupport support;

#ifdef _WIN32
#ifdef NETCOPY_WITH_RIO
    support.rio_compiled = true;
    if (socket != INVALID_SOCKET_VALUE) {
        GUID function_table_id = WSAID_MULTIPLE_RIO;
        RIO_EXTENSION_FUNCTION_TABLE table{};
        DWORD bytes = 0;
        support.rio_available =
            WSAIoctl(socket,
                     SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                     &function_table_id,
                     sizeof(function_table_id),
                     &table,
                     sizeof(table),
                     &bytes,
                     nullptr,
                     nullptr) == 0;
    }
#endif

#ifdef NETCOPY_WITH_IORING
    support.ioring_compiled = true;
#ifdef NETCOPY_HAS_IORING_HEADER
    HIORING ring = nullptr;
    IORING_CREATE_FLAGS flags{};
    flags.Required = IORING_CREATE_REQUIRED_FLAGS_NONE;
    flags.Advisory = IORING_CREATE_ADVISORY_FLAGS_NONE;
    HRESULT hr = CreateIoRing(IORING_VERSION_3, flags, 0, 0, &ring);
    support.ioring_available = SUCCEEDED(hr) && ring != nullptr;
    if (ring) {
        CloseIoRing(ring);
    }
#endif
#endif

#ifdef NETCOPY_WITH_MSQUIC
    support.msquic_compiled = true;
#endif
#else
    (void)socket;
#endif

    return support;
}

uint64_t recommended_tcp_inflight_window(socket_t socket, uint64_t fallback_bytes) {
    uint64_t fallback = fallback_bytes == 0 ? 64ull * 1024ull * 1024ull : fallback_bytes;

#if defined(_WIN32) && defined(NETCOPY_WITH_TCP_INFO_WINDOW) && defined(SIO_TCP_INFO)
    if (socket == INVALID_SOCKET_VALUE) {
        return clamp_window(fallback, fallback);
    }

    TCP_INFO_v0 info{};
    DWORD bytes = 0;
    int result = WSAIoctl(socket,
                          SIO_TCP_INFO,
                          nullptr,
                          0,
                          &info,
                          sizeof(info),
                          &bytes,
                          nullptr,
                          nullptr);
    if (result == 0) {
        uint64_t cwnd = static_cast<uint64_t>(info.Cwnd);
        uint64_t mss = static_cast<uint64_t>(info.Mss);
        uint64_t bytes_in_flight = static_cast<uint64_t>(info.BytesInFlight);
        uint64_t candidate = cwnd;

        if (mss > 0 && candidate > 0 && candidate < mss * 4) {
            candidate *= mss;
        }
        if (bytes_in_flight > candidate) {
            candidate = bytes_in_flight * 2;
        } else {
            candidate *= 2;
        }
        return clamp_window(candidate, fallback);
    }
#else
    (void)socket;
#endif

    return clamp_window(fallback, fallback);
}

std::string feature_summary(const FeatureSupport& support) {
    std::ostringstream out;
    out << "Windows experimental features:"
        << " RIO=" << (support.rio_compiled ? (support.rio_available ? "available" : "compiled") : "off")
        << ", IORing=" << (support.ioring_compiled ? (support.ioring_available ? "available" : "compiled") : "off")
        << ", MsQuic=" << (support.msquic_compiled ? "compiled" : "off");
    return out.str();
}

} // namespace windows_experimental
} // namespace network
} // namespace netcopy
