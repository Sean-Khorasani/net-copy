#include "config/config_parser.h"
#include "exceptions.h"
#include "common/utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstdint> // Added for uint64_t

namespace netcopy {
namespace config {

void ConfigParser::load_from_file(const std::string& filename) {
    std::ifstream file(std::filesystem::u8path(filename));
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
    std::ofstream file(std::filesystem::u8path(filename));
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

void ConfigParser::delete_section(const std::string& section) {
    data_.erase(section);
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
    config.udp = parser.get_bool("network", "udp", false);
    config.socket_buffer_size = parser.get_int("network", "socket_buffer_size", 0);
    
    config.default_protocol = parser.get_string("protocol", "default_protocol", "internal");
    
    // Protocol Internal
    config.internal.enable = parser.get_bool("protocol.internal", "enable", true);
    config.internal.secret_key = parser.get_string("protocol.internal", "secret_key", "");
    config.internal.require_auth = parser.get_bool("protocol.internal", "require_auth", true);
    config.internal.auth_method = parser.get_string("protocol.internal", "auth_method", "password");
    config.internal.security_level = parser.get_string("protocol.internal", "security_level", "auto");
    config.internal.users_file = parser.get_string("protocol.internal", "users_file", "users.csv");
    config.internal.allow_anonymous = parser.get_bool("protocol.internal", "allow_anonymous", false);
    
    std::string max_chunk_str = parser.get_string("protocol.internal", "max_chunk_size", "adaptive");
    if (max_chunk_str == "adaptive" || max_chunk_str == "automatic") {
        config.internal.adaptive_chunk_size = true;
        config.internal.max_chunk_size = 10485760;
    } else {
        config.internal.adaptive_chunk_size = false;
        config.internal.max_chunk_size = static_cast<size_t>(std::stoull(max_chunk_str.empty() ? "10485760" : max_chunk_str));
    }
    
    // Protocol TLS
    config.tls.enable = parser.get_bool("protocol.tls", "enable", false);
    config.tls.server_cert_file = parser.get_string("protocol.tls", "tls_server_cert_file", "");
    config.tls.server_key_file = parser.get_string("protocol.tls", "tls_server_key_file", "");
    config.tls.dh_file = parser.get_string("protocol.tls", "tls_dh_file", "");
    config.tls.client_cert_validation = parser.get_bool("protocol.tls", "tls_client_cert_validation", false);
    config.tls.client_chain_validation = parser.get_bool("protocol.tls", "tls_client_chain_validation", false);
    config.tls.trusted_chain_file = parser.get_string("protocol.tls", "tls_trusted_chain_file", "");
    
    // Protocol SSH
    config.ssh.enable = parser.get_bool("protocol.ssh", "enable", false);
    config.ssh.port = static_cast<uint16_t>(parser.get_int("protocol.ssh", "port", 2222));
    
    // Protocol SFTP
    config.sftp.enable = parser.get_bool("protocol.sftp", "enable", false);
    
    // Logging
    config.logging.enable = parser.get_bool("logging", "enable", true);
    config.logging.level = parser.get_string("logging", "log_level", "INFO");
    config.logging.file = parser.get_string("logging", "log_file", "server.log");
    config.logging.format = parser.get_string("logging", "log_format", "text");
    config.logging.audit_file = parser.get_string("logging", "audit_file", "");
    
    // Console
    config.console.enable = parser.get_bool("console_output", "enable", true);
    config.console.level = parser.get_string("console_output", "level", "INFO");
    
    config.max_file_size = parser.get_uint64("performance", "max_file_size", 0);
    config.max_bandwidth_percent = parser.get_int("performance", "max_bandwidth_percent", 0);
    
    config.webhook_url = parser.get_string("integration", "webhook_url", "");
    
    config.run_as_daemon = parser.get_bool("daemon", "run_as_daemon", false);
    config.pid_file = parser.get_string("daemon", "pid_file", "/var/run/net_copy_server.pid");
    
    config.allowed_paths = parser.get_string_list("paths", "allowed_paths", {"/var/lib/net_copy"});
    for (auto& p : config.allowed_paths) {
        size_t start = p.find_first_not_of(" \t\r\n\"'");
        size_t end = p.find_last_not_of(" \t\r\n\"'");
        if (start != std::string::npos && end != std::string::npos) {
            p = p.substr(start, end - start + 1);
        } else {
            p = "";
        }
        p = common::convert_to_native_path(p);
    }
    config.auto_create_directories = parser.get_bool("paths", "auto_create_directories", true);
    
    // Fallbacks for older config formats
    if (!parser.has_key("protocol.internal", "secret_key") && parser.has_key("security", "secret_key")) {
        config.internal.secret_key = parser.get_string("security", "secret_key", "");
        config.internal.require_auth = parser.get_bool("security", "require_auth", true);
        config.tls.enable = parser.get_bool("security", "tls", false);
        config.tls.server_cert_file = parser.get_string("security", "tls_cert_file", "");
        config.tls.server_key_file = parser.get_string("security", "tls_key_file", "");
    }
    
    return config;
}

ServerConfig ServerConfig::get_default() {
    ServerConfig config;
    config.listen_address = "0.0.0.0";
    config.listen_port = 1245;
    config.max_connections = 10;
    config.timeout = 30;
    config.udp = false;
    config.socket_buffer_size = 0;
    
    config.default_protocol = "internal";
    
    config.internal.enable = true;
    config.internal.secret_key = "";
    config.internal.require_auth = true;
    config.internal.auth_method = "password";
    config.internal.security_level = "auto";
    config.internal.users_file = "users.csv";
    config.internal.allow_anonymous = false;
    config.internal.max_chunk_size = 10485760;
    config.internal.adaptive_chunk_size = true;
    
    config.tls.enable = false;
    config.tls.server_cert_file = "";
    config.tls.server_key_file = "";
    config.tls.dh_file = "";
    config.tls.client_cert_validation = false;
    config.tls.client_chain_validation = false;
    config.tls.trusted_chain_file = "";
    
    config.ssh.enable = false;
    config.ssh.port = 2222;
    
    config.sftp.enable = false;
    
    config.logging.enable = true;
    config.logging.level = "INFO";
    config.logging.file = "server.log";
    config.logging.format = "text";
    config.logging.audit_file = "";
    
    config.console.enable = true;
    config.console.level = "INFO";
    
    config.max_file_size = 0;
    config.max_bandwidth_percent = 0;
    config.webhook_url = "";
    config.run_as_daemon = false;
    config.pid_file = "/var/run/net_copy_server.pid";
    config.allowed_paths = {"/var/lib/net_copy"};
    config.auto_create_directories = true;
    
    return config;
}

// ClientConfig implementation
ClientConfig ClientConfig::load_from_file(const std::string& filename) {
    ConfigParser parser;
    parser.load_from_file(filename);
    
    ClientConfig config;
    config.timeout = parser.get_int("connection", "timeout", 30);
    config.keep_alive = parser.get_bool("connection", "keep_alive", true);
    config.udp = parser.get_bool("network", "udp", false);
    config.socket_buffer_size = parser.get_int("network", "socket_buffer_size", 0);
    
    config.default_protocol = parser.get_string("protocol", "default_protocol", "internal");
    
    // Protocol Internal
    config.internal.enable = parser.get_bool("protocol.internal", "enable", true);
    config.internal.secret_key = parser.get_string("protocol.internal", "secret_key", "");
    config.internal.username = parser.get_string("protocol.internal", "username", "");
    config.internal.password = parser.get_string("protocol.internal", "password", "");
    config.internal.password_encrypted = parser.get_string("protocol.internal", "password_encrypted", "");
    config.internal.auth_method = parser.get_string("protocol.internal", "auth_method", "none");
    config.internal.security_level = parser.get_string("protocol.internal", "security_level", "HIGH");
    config.internal.private_key_file = parser.get_string("protocol.internal", "private_key_file", "");
    config.internal.private_key_passphrase = parser.get_string("protocol.internal", "private_key_passphrase", "");
    config.internal.initial_chunk_size = static_cast<size_t>(parser.get_int("protocol.internal", "initial_chunk_size", 262144));
    config.internal.min_chunk_size = static_cast<size_t>(parser.get_int("protocol.internal", "min_chunk_size", 8192));
    std::string max_chunk_str = parser.get_string("protocol.internal", "max_chunk_size", "adaptive");
    if (max_chunk_str == "adaptive" || max_chunk_str == "automatic") {
        config.internal.max_chunk_size = 10485760;
    } else {
        config.internal.max_chunk_size = static_cast<size_t>(std::stoull(max_chunk_str.empty() ? "10485760" : max_chunk_str));
    }
    
    config.internal.chunk_size_increase_factor = std::stod(parser.get_string("protocol.internal", "chunk_size_increase_factor", "1.1"));
    config.internal.chunk_size_decrease_factor = std::stod(parser.get_string("protocol.internal", "chunk_size_decrease_factor", "0.5"));
    
    // Protocol TLS
    config.tls.enable = parser.get_bool("protocol.tls", "enable", false);
    config.tls.mutual_authentication = parser.get_bool("protocol.tls", "tls_mutual_authentication", false);
    config.tls.client_cert_file = parser.get_string("protocol.tls", "tls_client_cert_file", "");
    config.tls.client_key_file = parser.get_string("protocol.tls", "tls_client_key_file", "");
    config.tls.server_cert_validation = parser.get_bool("protocol.tls", "tls_server_cert_validation", true);
    config.tls.server_chain_validation = parser.get_bool("protocol.tls", "tls_server_chain_validation", true);
    config.tls.trusted_chain_file = parser.get_string("protocol.tls", "tls_trusted_chain_file", "");
    
    // Protocol SSH
    config.ssh.enable = parser.get_bool("protocol.ssh", "enable", false);
    config.ssh.username = parser.get_string("protocol.ssh", "username", "");
    config.ssh.private_key_file = parser.get_string("protocol.ssh", "private_key_file", "");
    
    // Protocol SFTP
    config.sftp.enable = parser.get_bool("protocol.sftp", "enable", false);
    
    // Performance
    config.max_bandwidth_percent = parser.get_int("performance", "max_bandwidth_percent", 0);
    config.retry_attempts = parser.get_int("performance", "retry_attempts", 3);
    config.retry_delay = parser.get_int("performance", "retry_delay", 5);
    
    // Logging
    config.logging.enable = parser.get_bool("logging", "enable", true);
    config.logging.level = parser.get_string("logging", "log_level", "INFO");
    config.logging.file = parser.get_string("logging", "log_file", "client.log");
    config.logging.format = parser.get_string("logging", "log_format", "text");
    
    // Console
    config.console.enable = parser.get_bool("console_output", "enable", true);
    config.console.level = parser.get_string("console_output", "level", "INFO");
    
    config.create_empty_directories = parser.get_bool("transfer", "create_empty_directories", true);
    config.auto_create_directories = parser.get_bool("transfer", "auto_create_directories", true);
    
    config.proxy_type = parser.get_string("proxy", "type", "none");
    config.proxy_host = parser.get_string("proxy", "host", "");
    config.proxy_port = static_cast<uint16_t>(parser.get_int("proxy", "port", 0));
    config.proxy_username = parser.get_string("proxy", "username", "");
    config.proxy_password = parser.get_string("proxy", "password", "");
    config.webhook_url = parser.get_string("integration", "webhook_url", "");
    
    // GUI settings
    config.gui.port = static_cast<uint16_t>(parser.get_int("gui", "port", 1246));
    config.gui.open_browser_on_start = parser.get_bool("gui", "open_browser_on_start", true);
    config.gui.theme = parser.get_string("gui", "theme", "system");
    config.gui.language = parser.get_string("gui", "language", "en");
    
    // Fallbacks for older config formats
    if (!parser.has_key("protocol.internal", "secret_key") && parser.has_key("security", "secret_key")) {
        config.internal.secret_key = parser.get_string("security", "secret_key", "");
        config.tls.enable = parser.get_bool("security", "tls", false);
        config.tls.server_cert_validation = parser.get_bool("security", "tls_verify", true);
        config.tls.trusted_chain_file = parser.get_string("security", "tls_ca_file", "");
        
        config.internal.username = parser.get_string("auth", "username", "");
        config.internal.password = parser.get_string("auth", "password", "");
        config.internal.auth_method = parser.get_string("auth", "auth_method", "none");
        config.internal.private_key_file = parser.get_string("auth", "private_key_file", "");
    }
    
    return config;
}

ClientConfig ClientConfig::get_default() {
    ClientConfig config;
    config.timeout = 30;
    config.keep_alive = true;
    config.udp = false;
    config.socket_buffer_size = 0;
    
    config.default_protocol = "internal";
    
    config.internal.enable = true;
    config.internal.secret_key = "";
    config.internal.username = "";
    config.internal.password = "";
    config.internal.password_encrypted = "";
    config.internal.auth_method = "none";
    config.internal.security_level = "HIGH";
    config.internal.private_key_file = "";
    config.internal.private_key_passphrase = "";
    config.internal.initial_chunk_size = 262144;
    config.internal.min_chunk_size = 8192;
    config.internal.max_chunk_size = 10485760;
    config.internal.chunk_size_increase_factor = 1.1;
    config.internal.chunk_size_decrease_factor = 0.5;
    
    config.tls.enable = false;
    config.tls.mutual_authentication = false;
    config.tls.client_cert_file = "";
    config.tls.client_key_file = "";
    config.tls.server_cert_validation = true;
    config.tls.server_chain_validation = true;
    config.tls.trusted_chain_file = "";
    
    config.ssh.enable = false;
    config.ssh.username = "";
    config.ssh.private_key_file = "";
    
    config.sftp.enable = false;
    
    config.max_bandwidth_percent = 0;
    config.retry_attempts = 3;
    config.retry_delay = 5;
    
    config.logging.enable = true;
    config.logging.level = "INFO";
    config.logging.file = "client.log";
    config.logging.format = "text";
    
    config.console.enable = true;
    config.console.level = "INFO";
    
    config.create_empty_directories = true;
    config.auto_create_directories = true;
    
    config.proxy_type = "none";
    config.proxy_host = "";
    config.proxy_port = 0;
    config.proxy_username = "";
    config.proxy_password = "";
    config.webhook_url = "";
    
    return config;
}

} // namespace config
} // namespace netcopy

