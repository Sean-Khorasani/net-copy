#pragma once

#include <cstdint>
#include <cstddef>

namespace netcopy {
namespace config {
namespace defaults {

inline constexpr uint16_t kDefaultTransferPort = 1245;
inline constexpr int kDefaultTimeoutSeconds = 30;
inline constexpr int kDefaultSocketBufferSize = 0;
inline constexpr int kDefaultMaxBandwidthPercent = 0;
inline constexpr uint64_t kUnlimitedFileSize = 0;

inline constexpr size_t kInitialChunkSize = 262144;
inline constexpr size_t kMinChunkSize = 8192;
inline constexpr size_t kMaxChunkSize = 10485760;
inline constexpr size_t kMaxFrameSize = 64 * 1024 * 1024;
inline constexpr double kChunkSizeIncreaseFactor = 1.1;
inline constexpr double kChunkSizeDecreaseFactor = 0.5;

inline constexpr const char* kProtocolInternal = "internal";
inline constexpr const char* kProtocolSsh = "ssh";
inline constexpr const char* kProtocolSftp = "sftp";

inline constexpr const char* kAuthNone = "none";
inline constexpr const char* kAuthPassword = "password";
inline constexpr const char* kAuthMlKem = "mlkem";

inline constexpr const char* kSecurityAuto = "auto";
inline constexpr const char* kSecurityHigh = "HIGH";
inline constexpr const char* kSecurityFast = "fast";
inline constexpr const char* kSecurityAes = "aes";
inline constexpr const char* kSecurityAes256Gcm = "AES-256-GCM";

inline constexpr const char* kLogLevelInfo = "INFO";
inline constexpr const char* kLogLevelDebug = "DEBUG";
inline constexpr const char* kLogFormatText = "text";
inline constexpr const char* kLogFormatJson = "json";

inline constexpr int kMinPort = 1;
inline constexpr int kMaxPort = 65535;
inline constexpr int kMinPositiveValue = 1;
inline constexpr int kMaxConnectionsLimit = 100000;
inline constexpr int kMaxTimeoutSeconds = 86400;
inline constexpr int kMaxSocketBufferSize = 1024 * 1024 * 1024;
inline constexpr int kMaxPercent = 100;

} // namespace defaults
} // namespace config
} // namespace netcopy
