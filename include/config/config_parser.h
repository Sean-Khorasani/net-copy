#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint> // Added for uint64_t, uint16_t
#include <unordered_map>

namespace netcopy {
namespace config {

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

private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;
    
    std::string trim(const std::string& str) const;
    std::vector<std::string> split(const std::string& str, char delimiter) const;
};

// Server configuration structure
struct ServerConfig {
    // Network settings
    std::string listen_address;
    uint16_t listen_port;
    int max_connections;
    int timeout;
    bool udp;
    int socket_buffer_size;
    
    // Protocol settings
    std::string default_protocol;
    
    struct ProtocolInternal {
        bool enable = true;
        std::string secret_key;
        bool require_auth = true;
        std::string auth_method = "password";
        std::string security_level = "auto";
        std::string users_file = "users.csv";
        bool allow_anonymous = false;
        size_t max_chunk_size = 10485760;
        bool adaptive_chunk_size = true;
    } internal;
    
    struct ProtocolTls {
        bool enable = false;
        std::string server_cert_file;
        std::string server_key_file;
        std::string dh_file;
        bool client_cert_validation = false;
        bool client_chain_validation = false;
        std::string trusted_chain_file;
    } tls;
    
    struct ProtocolSsh {
        bool enable = false;
        uint16_t port = 2222;
    } ssh;
    
    struct ProtocolSftp {
        bool enable = false;
    } sftp;
    
    // Logging settings
    struct Logging {
        bool enable = true;
        std::string level = "INFO";
        std::string file = "server.log";
        std::string format = "text";
        std::string audit_file = "";
    } logging;
    
    struct Console {
        bool enable = true;
        std::string level = "INFO";
    } console;
    
    // Performance and Limits
    uint64_t max_file_size;
    int max_bandwidth_percent;
    
    // Integration
    std::string webhook_url;
    
    // Daemon settings
    bool run_as_daemon;
    std::string pid_file;
    
    // Path settings
    std::vector<std::string> allowed_paths;
    bool auto_create_directories;
    
    static ServerConfig load_from_file(const std::string& filename);
    static ServerConfig get_default();
};

// Client configuration structure
struct ClientConfig {
    // Connection settings
    int timeout;
    bool keep_alive;
    bool udp;
    int socket_buffer_size;
    
    // Protocol settings
    std::string default_protocol;
    
    struct ProtocolInternal {
        bool enable = true;
        std::string secret_key;
        std::string username;
        std::string password;
        std::string password_encrypted;
        std::string auth_method = "none";
        std::string security_level = "HIGH";
        std::string private_key_file;
        std::string private_key_passphrase;
        size_t initial_chunk_size = 262144;
        size_t min_chunk_size = 8192;
        size_t max_chunk_size = 10485760;
        double chunk_size_increase_factor = 1.1;
        double chunk_size_decrease_factor = 0.5;
    } internal;
    
    struct ProtocolTls {
        bool enable = false;
        bool mutual_authentication = false;
        std::string client_cert_file;
        std::string client_key_file;
        bool server_cert_validation = true;
        bool server_chain_validation = true;
        std::string trusted_chain_file;
    } tls;
    
    struct ProtocolSsh {
        bool enable = false;
        std::string username;
        std::string private_key_file;
    } ssh;
    
    struct ProtocolSftp {
        bool enable = false;
    } sftp;
    
    // Performance settings
    int max_bandwidth_percent;
    int retry_attempts;
    int retry_delay;
    
    // Logging settings
    struct Logging {
        bool enable = true;
        std::string level = "INFO";
        std::string file = "client.log";
        std::string format = "text";
    } logging;
    
    struct Console {
        bool enable = true;
        std::string level = "INFO";
    } console;
    
    // Transfer settings
    bool create_empty_directories;
    bool auto_create_directories;
    
    // Proxy settings
    std::string proxy_type = "none";
    std::string proxy_host;
    uint16_t proxy_port = 0;
    std::string proxy_username;
    std::string proxy_password;
    std::string webhook_url;
    
    struct GuiSettings {
        uint16_t port = 1246;
        bool open_browser_on_start = true;
        std::string theme = "system";
        std::string language = "en";
    } gui;
    
    static ClientConfig load_from_file(const std::string& filename);
    static ClientConfig get_default();
};

} // namespace config
} // namespace netcopy

