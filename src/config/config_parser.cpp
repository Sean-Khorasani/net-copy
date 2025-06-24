#include "config/config_parser.h"
#include "exceptions.h"
#include "common/utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdint> // Added for uint64_t

namespace netcopy {
namespace config {

void ConfigParser::load_from_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        throw ConfigException("Failed to open config file: " + filename);
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    load_from_string(content);
}

void ConfigParser::load_from_string(const std::string& config_data) {
    std::istringstream stream(config_data);
    std::string line;
    std::string current_section;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Check for section header
        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = trim(current_section);
            continue;
        }
        
        // Parse key-value pair
        size_t equals_pos = line.find('=');
        if (equals_pos != std::string::npos) {
            std::string key = trim(line.substr(0, equals_pos));
            std::string value = trim(line.substr(equals_pos + 1));
            
            // Remove quotes if present
            if (value.length() >= 2 && 
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.length() - 2);
            }
            
            // Handle multiple values for the same key (append with comma)
            if (data_[current_section].find(key) != data_[current_section].end() && 
                !data_[current_section][key].empty()) {
                data_[current_section][key] += "," + value;
            } else {
                data_[current_section][key] = value;
            }
        }
    }
}

std::string ConfigParser::get_string(const std::string& section, const std::string& key, const std::string& default_value) const {
    auto section_it = data_.find(section);
    if (section_it == data_.end()) {
        return default_value;
    }
    
    auto key_it = section_it->second.find(key);
    if (key_it == section_it->second.end()) {
        return default_value;
    }
    
    return key_it->second;
}

int ConfigParser::get_int(const std::string& section, const std::string& key, int default_value) const {
    std::string value = get_string(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return default_value;
    }
}

uint64_t ConfigParser::get_uint64(const std::string& section, const std::string& key, uint64_t default_value) const {
    std::string value = get_string(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    try {
        return std::stoull(value);
    } catch (const std::exception&) {
        return default_value;
    }
}

bool ConfigParser::get_bool(const std::string& section, const std::string& key, bool default_value) const {
    std::string value = get_string(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    return value == "true" || value == "yes" || value == "1" || value == "on";
}

std::vector<std::string> ConfigParser::get_string_list(const std::string& section, const std::string& key, const std::vector<std::string>& default_value) const {
    std::string value = get_string(section, key);
    if (value.empty()) {
        return default_value;
    }
    
    return split(value, ',');
}

void ConfigParser::set_string(const std::string& section, const std::string& key, const std::string& value) {
    data_[section][key] = value;
}

void ConfigParser::set_int(const std::string& section, const std::string& key, int value) {
    data_[section][key] = std::to_string(value);
}

void ConfigParser::set_uint64(const std::string& section, const std::string& key, uint64_t value) {
    data_[section][key] = std::to_string(value);
}

void ConfigParser::set_bool(const std::string& section, const std::string& key, bool value) {
    data_[section][key] = value ? "true" : "false";
}

void ConfigParser::save_to_file(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file) {
        throw ConfigException("Failed to open config file for writing: " + filename);
    }
    
    file << save_to_string();
}

std::string ConfigParser::save_to_string() const {
    std::ostringstream stream;
    
    for (const auto& section_pair : data_) {
        stream << "[" << section_pair.first << "]\n";
        
        for (const auto& key_pair : section_pair.second) {
            stream << key_pair.first << " = " << key_pair.second << "\n";
        }
        
        stream << "\n";
    }
    
    return stream.str();
}

bool ConfigParser::has_key(const std::string& section, const std::string& key) const {
    auto section_it = data_.find(section);
    if (section_it == data_.end()) {
        return false;
    }
    
    return section_it->second.find(key) != section_it->second.end();
}

std::vector<std::string> ConfigParser::get_sections() const {
    std::vector<std::string> sections;
    for (const auto& pair : data_) {
        sections.push_back(pair.first);
    }
    return sections;
}

std::vector<std::string> ConfigParser::get_keys(const std::string& section) const {
    std::vector<std::string> keys;
    auto section_it = data_.find(section);
    if (section_it != data_.end()) {
        for (const auto& pair : section_it->second) {
            keys.push_back(pair.first);
        }
    }
    return keys;
}

std::string ConfigParser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> ConfigParser::split(const std::string& str, char delimiter) const {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    
    while (std::getline(stream, token, delimiter)) {
        token = trim(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

// ServerConfig implementation
ServerConfig ServerConfig::load_from_file(const std::string& filename) {
    ConfigParser parser;
    parser.load_from_file(filename);
    
    ServerConfig config;
    config.listen_address = parser.get_string("network", "listen_address", "0.0.0.0");
    config.listen_port = static_cast<uint16_t>(parser.get_int("network", "listen_port", 1245));
    config.max_connections = parser.get_int("network", "max_connections", 10);
    config.timeout = parser.get_int("network", "timeout", 30);
    
    config.secret_key = parser.get_string("security", "secret_key", "");
    config.require_auth = parser.get_bool("security", "require_auth", true);
    config.max_file_size = parser.get_uint64("security", "max_file_size", 1073741824);
    
    config.buffer_size = static_cast<size_t>(parser.get_int("performance", "buffer_size", 65536));
    config.max_bandwidth_percent = parser.get_int("performance", "max_bandwidth_percent", 40);
    config.thread_pool_size = parser.get_int("performance", "thread_pool_size", 4);
    
    config.log_level = parser.get_string("logging", "log_level", "INFO");
    config.log_file = parser.get_string("logging", "log_file", "server.log");
    config.console_output = parser.get_bool("logging", "console_output", true);
    
    config.run_as_daemon = parser.get_bool("daemon", "run_as_daemon", false);
    config.pid_file = parser.get_string("daemon", "pid_file", "/var/run/net_copy_server.pid");
    
    config.allowed_paths = parser.get_string_list("paths", "allowed_paths", {"/var/lib/net_copy"});
    
    return config;
}

ServerConfig ServerConfig::get_default() {
    ServerConfig config;
    config.listen_address = "0.0.0.0";
    config.listen_port = 1245;
    config.max_connections = 10;
    config.timeout = 30;
    
    config.secret_key = "";
    config.require_auth = true;
    config.max_file_size = 1073741824;
    
    config.buffer_size = 65536;
    config.max_bandwidth_percent = 40;
    config.thread_pool_size = 4;
    
    config.log_level = "INFO";
    config.log_file = "server.log";
    config.console_output = true;
    
    config.run_as_daemon = false;
    config.pid_file = "/var/run/net_copy_server.pid";
    
    config.allowed_paths = {"/var/lib/net_copy"};
    
    return config;
}

// ClientConfig implementation
ClientConfig ClientConfig::load_from_file(const std::string& filename) {
    ConfigParser parser;
    parser.load_from_file(filename);
    
    ClientConfig config;
    config.secret_key = parser.get_string("security", "secret_key", "");
    
    config.buffer_size = static_cast<size_t>(parser.get_int("performance", "buffer_size", 65536));
    config.max_bandwidth_percent = parser.get_int("performance", "max_bandwidth_percent", 40);
    config.retry_attempts = parser.get_int("performance", "retry_attempts", 3);
    config.retry_delay = parser.get_int("performance", "retry_delay", 5);
    
    config.log_level = parser.get_string("logging", "log_level", "INFO");
    config.log_file = parser.get_string("logging", "log_file", "client.log");
    config.console_output = parser.get_bool("logging", "console_output", true);
    
    config.timeout = parser.get_int("connection", "timeout", 30);
    config.keep_alive = parser.get_bool("connection", "keep_alive", true);
    
    // Transfer settings
    config.create_empty_directories = parser.get_bool("transfer", "create_empty_directories", true);
    
    return config;
}

ClientConfig ClientConfig::get_default() {
    ClientConfig config;
    config.secret_key = "";
    
    config.buffer_size = 65536;
    config.max_bandwidth_percent = 40;
    config.retry_attempts = 3;
    config.retry_delay = 5;
    
    config.log_level = "INFO";
    config.log_file = "client.log";
    config.console_output = true;
    
    config.timeout = 30;
    config.keep_alive = true;
    
    // Transfer settings defaults
    config.create_empty_directories = true;  // Default to true
    
    return config;
}

} // namespace config
} // namespace netcopy

