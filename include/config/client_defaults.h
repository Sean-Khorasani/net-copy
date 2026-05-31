#pragma once

#include "config/common_defaults.h"

namespace netcopy {
namespace config {
namespace defaults {

inline constexpr const char* kClientConfigFileName = "client.conf";
inline constexpr bool kClientKeepAlive = true;
inline constexpr bool kClientUdpEnabled = false;

inline constexpr bool kClientInternalEnabled = true;
inline constexpr bool kClientTlsEnabled = false;
inline constexpr bool kClientTlsMutualAuthentication = false;
inline constexpr bool kClientTlsServerCertValidation = true;
inline constexpr bool kClientTlsServerChainValidation = true;
inline constexpr bool kClientSshEnabled = false;
inline constexpr bool kClientSftpEnabled = false;

inline constexpr int kClientRetryAttempts = 3;
inline constexpr int kClientRetryDelaySeconds = 5;
inline constexpr bool kClientLoggingEnabled = true;
inline constexpr const char* kClientLogFile = "client.log";
inline constexpr const char* kGuiLogFile = "net_copy_gui.log";
inline constexpr bool kClientConsoleEnabled = true;
inline constexpr bool kCreateEmptyDirectories = true;
inline constexpr bool kAutoCreateDirectories = true;

inline constexpr const char* kProxyNone = "none";
inline constexpr const char* kProxySocks5 = "socks5";
inline constexpr const char* kProxyHttp = "http";

inline constexpr uint16_t kGuiPort = 1246;
inline constexpr bool kGuiOpenBrowserOnStart = true;
inline constexpr const char* kGuiThemeSystem = "system";
inline constexpr const char* kGuiThemeLight = "light";
inline constexpr const char* kGuiThemeDark = "dark";
inline constexpr const char* kGuiLanguage = "en";

inline constexpr int kMaxRetryAttempts = 100;

} // namespace defaults
} // namespace config
} // namespace netcopy
