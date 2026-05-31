#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint> // Added for uint64_t, uint16_t
#include <unordered_map>
#include "config/common_defaults.h"
#include "config/server_defaults.h"
#include "config/client_defaults.h"

namespace netcopy {
namespace config {

struct ConfigValidationIssue {
    int line = 0;
    std::string message;
};

class ConfigParser {
public:
    ConfigParser() = default;
    
    // Load configuration from file
    void load_from_file(const std::string& filename);
    
    // Load configuration from string
    void load_from_string(const std::string& config_data);
    
    // Get values
    std::string get_string(const std::string& section, const std::string& key, const std::string& default_value = "") const;
    int get_int(const std::string& section, const std::string& key, int default_value = 0) const;
    uint64_t get_uint64(const std::string& section, const std::string& key, uint64_t default_value = 0) const;
    bool get_bool(const std::string& section, const std::string& key, bool default_value = false) const;
    std::vector<std::string> get_string_list(const std::string& section, const std::string& key, const std::vector<std::string>& default_value = {}) const;
    
    // Set values
    void set_string(const std::string& section, const std::string& key, const std::string& value);
    void set_int(const std::string& section, const std::string& key, int value);
    void set_uint64(const std::string& section, const std::string& key, uint64_t value);
    void set_bool(const std::string& section, const std::string& key, bool value);
    
    // Save configuration
    void save_to_file(const std::string& filename) const;
    std::string save_to_string() const;
    
    // Check if key exists
    bool has_key(const std::string& section, const std::string& key) const;
    void delete_section(const std::string& section);
    
    // Get all sections
    std::vector<std::string> get_sections() const;
    
    // Get all keys in a section
    std::vector<std::string> get_keys(const std::string& section) const;

    static std::string format_validation_issues(const std::vector<ConfigValidationIssue>& issues);

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
    
    std::string trim(const std::string& str) const;
    std::vector<std::string> split(const std::string& str, char delimiter) const;
};

// Server configuration structure
struct ServerConfig {
    // Network settings
    std::string listen_address = defaults::kServerListenAddress;
    uint16_t listen_port = defaults::kDefaultTransferPort;
    int max_connections = defaults::kServerMaxConnections;
    int timeout = defaults::kDefaultTimeoutSeconds;
    bool udp = defaults::kServerUdpEnabled;
    int socket_buffer_size = defaults::kDefaultSocketBufferSize;
    
    // Protocol settings
    std::string default_protocol = defaults::kProtocolInternal;
    
    struct ProtocolInternal {
        bool enable = defaults::kServerInternalEnabled;
        std::string secret_key;
        bool require_auth = defaults::kServerRequireAuth;
        std::string auth_method = defaults::kAuthPassword;
        std::string security_level = defaults::kSecurityAuto;
        std::string users_file = defaults::kServerUsersFile;
        bool allow_anonymous = defaults::kServerAllowAnonymous;
        size_t max_chunk_size = defaults::kMaxChunkSize;
        bool adaptive_chunk_size = defaults::kServerAdaptiveChunkSize;
    } internal;
    
    struct ProtocolTls {
        bool enable = defaults::kServerTlsEnabled;
        std::string server_cert_file;
        std::string server_key_file;
        std::string dh_file;
        bool client_cert_validation = defaults::kServerTlsClientCertValidation;
        bool client_chain_validation = defaults::kServerTlsClientChainValidation;
        std::string trusted_chain_file;
    } tls;
    
    struct ProtocolSsh {
        bool enable = defaults::kServerSshEnabled;
        uint16_t port = defaults::kServerSshPort;
    } ssh;
    
    struct ProtocolSftp {
        bool enable = defaults::kServerSftpEnabled;
    } sftp;
    
    // Logging settings
    struct Logging {
        bool enable = defaults::kServerLoggingEnabled;
        std::string level = defaults::kLogLevelInfo;
        std::string file = defaults::kServerLogFile;
        std::string format = defaults::kLogFormatText;
        std::string audit_file = defaults::kServerAuditFile;
    } logging;
    
    struct Console {
        bool enable = defaults::kServerConsoleEnabled;
        std::string level = defaults::kLogLevelInfo;
    } console;
    
    // Performance and Limits
    uint64_t max_file_size = defaults::kUnlimitedFileSize;
    int max_bandwidth_percent = defaults::kDefaultMaxBandwidthPercent;
    
    // Integration
    std::string webhook_url;
    
    // Daemon settings
    bool run_as_daemon = defaults::kServerRunAsDaemon;
    std::string pid_file = defaults::kServerPidFile;
    
    // Path settings
    std::vector<std::string> allowed_paths = {defaults::kServerAllowedPath};
    bool auto_create_directories = defaults::kServerAutoCreateDirectories;
    
    static ServerConfig load_from_file(const std::string& filename);
    static ServerConfig get_default();
    static void create_default_file(const std::string& filename);
    static std::vector<ConfigValidationIssue> validate_file(const std::string& filename);
};

// Client configuration structure
struct ClientConfig {
    // Connection settings
    int timeout = defaults::kDefaultTimeoutSeconds;
    bool keep_alive = defaults::kClientKeepAlive;
    bool udp = defaults::kClientUdpEnabled;
    int socket_buffer_size = defaults::kDefaultSocketBufferSize;
    
    // Protocol settings
    std::string default_protocol = defaults::kProtocolInternal;
    
    struct ProtocolInternal {
        bool enable = defaults::kClientInternalEnabled;
        std::string secret_key;
        std::string username;
        std::string password;
        std::string password_encrypted;
        std::string auth_method = defaults::kAuthNone;
        std::string security_level = defaults::kSecurityHigh;
        std::string private_key_file;
        std::string private_key_passphrase;
        size_t initial_chunk_size = defaults::kInitialChunkSize;
        size_t min_chunk_size = defaults::kMinChunkSize;
        size_t max_chunk_size = defaults::kMaxChunkSize;
        double chunk_size_increase_factor = defaults::kChunkSizeIncreaseFactor;
        double chunk_size_decrease_factor = defaults::kChunkSizeDecreaseFactor;
    } internal;
    
    struct ProtocolTls {
        bool enable = defaults::kClientTlsEnabled;
        bool mutual_authentication = defaults::kClientTlsMutualAuthentication;
        std::string client_cert_file;
        std::string client_key_file;
        bool server_cert_validation = defaults::kClientTlsServerCertValidation;
        bool server_chain_validation = defaults::kClientTlsServerChainValidation;
        std::string trusted_chain_file;
    } tls;
    
    struct ProtocolSsh {
        bool enable = defaults::kClientSshEnabled;
        std::string username;
        std::string private_key_file;
    } ssh;
    
    struct ProtocolSftp {
        bool enable = defaults::kClientSftpEnabled;
    } sftp;
    
    // Performance settings
    int max_bandwidth_percent = defaults::kDefaultMaxBandwidthPercent;
    int retry_attempts = defaults::kClientRetryAttempts;
    int retry_delay = defaults::kClientRetryDelaySeconds;
    
    // Logging settings
    struct Logging {
        bool enable = defaults::kClientLoggingEnabled;
        std::string level = defaults::kLogLevelInfo;
        std::string file = defaults::kClientLogFile;
        std::string format = defaults::kLogFormatText;
    } logging;
    
    struct Console {
        bool enable = defaults::kClientConsoleEnabled;
        std::string level = defaults::kLogLevelInfo;
    } console;
    
    // Transfer settings
    bool create_empty_directories = defaults::kCreateEmptyDirectories;
    bool auto_create_directories = defaults::kAutoCreateDirectories;
    
    // Proxy settings
    std::string proxy_type = defaults::kProxyNone;
    std::string proxy_host;
    uint16_t proxy_port = 0;
    std::string proxy_username;
    std::string proxy_password;
    std::string webhook_url;
    
    struct GuiSettings {
        uint16_t port = defaults::kGuiPort;
        bool open_browser_on_start = defaults::kGuiOpenBrowserOnStart;
        std::string theme = defaults::kGuiThemeSystem;
        std::string language = defaults::kGuiLanguage;
    } gui;
    
    static ClientConfig load_from_file(const std::string& filename);
    static ClientConfig get_default();
    static void create_default_file(const std::string& filename);
    static std::vector<ConfigValidationIssue> validate_file(const std::string& filename);
};

} // namespace config
} // namespace netcopy

