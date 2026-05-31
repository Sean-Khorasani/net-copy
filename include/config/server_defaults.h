#pragma once

#include "config/common_defaults.h"

namespace netcopy {
namespace config {
namespace defaults {

inline constexpr const char* kServerConfigFileName = "server.conf";
inline constexpr const char* kServerListenAddress = "0.0.0.0";
inline constexpr int kServerMaxConnections = 10;
inline constexpr bool kServerUdpEnabled = false;

inline constexpr bool kServerInternalEnabled = true;
inline constexpr bool kServerRequireAuth = true;
inline constexpr bool kServerAllowAnonymous = false;
inline constexpr bool kServerAdaptiveChunkSize = true;
inline constexpr const char* kServerUsersFile = "users.csv";

inline constexpr bool kServerTlsEnabled = false;
inline constexpr bool kServerTlsClientCertValidation = false;
inline constexpr bool kServerTlsClientChainValidation = false;
inline constexpr bool kServerSshEnabled = false;
inline constexpr uint16_t kServerSshPort = 2222;
inline constexpr bool kServerSftpEnabled = false;

inline constexpr bool kServerLoggingEnabled = true;
inline constexpr const char* kServerLogFile = "server.log";
inline constexpr const char* kServerAuditFile = "";
inline constexpr bool kServerConsoleEnabled = true;

inline constexpr bool kServerRunAsDaemon = false;
inline constexpr const char* kServerPidFile = "/var/run/net_copy_server.pid";
inline constexpr const char* kServerAllowedPath = "/var/lib/net_copy";
inline constexpr bool kServerAutoCreateDirectories = true;

} // namespace defaults
} // namespace config
} // namespace netcopy
