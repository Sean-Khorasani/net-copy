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
    
    // Security settings
    std::string secret_key;
    bool require_auth;
    uint64_t max_file_size;
    
    // Performance settings
    size_t buffer_size;
    int max_bandwidth_percent;
    int thread_pool_size;
    
    // Logging settings
    std::string log_level;
    std::string log_file;
    bool console_output;
    
    // Daemon settings
    bool run_as_daemon;
    std::string pid_file;
    
    // Path settings
    std::vector<std::string> allowed_paths;
    
    static ServerConfig load_from_file(const std::string& filename);
    static ServerConfig get_default();
};

// Client configuration structure
struct ClientConfig {
    // Security settings
    std::string secret_key;
    
    // Performance settings
    size_t buffer_size;
    int max_bandwidth_percent;
    int retry_attempts;
    int retry_delay;
    
    // Logging settings
    std::string log_level;
    std::string log_file;
    bool console_output;
    
    // Connection settings
    int timeout;
    bool keep_alive;
    
    // Transfer settings
    bool create_empty_directories;
    
    static ClientConfig load_from_file(const std::string& filename);
    static ClientConfig get_default();
};

} // namespace config
} // namespace netcopy

