#include "config/config_parser.h"
#include "exceptions.h"
#include "common/utils.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstdint> // Added for uint64_t
#include <set>
#include <unordered_set>
#include <cctype>

namespace netcopy {
namespace config {

namespace {

std::string trim_copy(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string strip_quotes(std::string value) {
    value = trim_copy(value);
    if (value.length() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.length() - 2);
    }
    return value;
}

bool is_valid_bool(const std::string& value) {
    std::string lower = lower_copy(value);
    return lower == "true" || lower == "false" || lower == "yes" || lower == "no" ||
           lower == "1" || lower == "0" || lower == "on" || lower == "off";
}

bool parse_int64(const std::string& value, int64_t& out) {
    try {
        size_t parsed = 0;
        out = std::stoll(value, &parsed);
        return parsed == value.size();
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_double_value(const std::string& value, double& out) {
    try {
        size_t parsed = 0;
        out = std::stod(value, &parsed);
        return parsed == value.size();
    } catch (const std::exception&) {
        return false;
    }
}

bool is_one_of(const std::string& value, const std::vector<std::string>& options) {
    std::string lower = lower_copy(value);
    for (const auto& option : options) {
        if (lower == lower_copy(option)) {
            return true;
        }
    }
    return false;
}

bool is_hex_secret_key(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    std::string key = value;
    if (key.size() > 2 && key[0] == '0' && (key[1] == 'x' || key[1] == 'X')) {
        key = key.substr(2);
    }
    if (key.size() != 64) {
        return false;
    }
    return std::all_of(key.begin(), key.end(),
                       [](unsigned char c) { return std::isxdigit(c) != 0; });
}

enum class ValueKind {
    String,
    Bool,
    IntRange,
    UInt64,
    DoubleRange,
    Option,
    ChunkSize
};

struct ConfigRule {
    const char* section;
    const char* key;
    ValueKind kind;
    double min_value = 0;
    double max_value = 0;
    std::vector<std::string> options;
};

std::string rule_key(const std::string& section, const std::string& key) {
    return section + "." + key;
}

std::unordered_map<std::string, ConfigRule> make_rule_map(const std::vector<ConfigRule>& rules) {
    std::unordered_map<std::string, ConfigRule> result;
    for (const auto& rule : rules) {
        result.emplace(rule_key(rule.section, rule.key), rule);
    }
    return result;
}

std::vector<std::string> log_level_options() {
    return {"DEBUG", "INFO", "WARNING", "WARN", "ERROR", "CRITICAL"};
}

std::vector<std::string> security_options(bool allow_auto) {
    std::vector<std::string> options = {"HIGH", "FAST", "AES", "AES-CTR", "AES-GCM", "AES_256_GCM", "AES-256-GCM"};
    if (allow_auto) {
        options.push_back("AUTO");
    }
    return options;
}

std::vector<ConfigRule> server_rules() {
    using namespace defaults;
    return {
        {"network", "listen_address", ValueKind::String},
        {"network", "listen_port", ValueKind::IntRange, kMinPort, kMaxPort},
        {"network", "max_connections", ValueKind::IntRange, kMinPositiveValue, kMaxConnectionsLimit},
        {"network", "timeout", ValueKind::IntRange, kMinPositiveValue, kMaxTimeoutSeconds},
        {"network", "udp", ValueKind::Bool},
        {"network", "socket_buffer_size", ValueKind::IntRange, 0, kMaxSocketBufferSize},
        {"protocol", "default_protocol", ValueKind::Option, 0, 0, {kProtocolInternal, kProtocolSsh, kProtocolSftp}},
        {"protocol.internal", "enable", ValueKind::Bool},
        {"protocol.internal", "secret_key", ValueKind::String},
        {"protocol.internal", "require_auth", ValueKind::Bool},
        {"protocol.internal", "auth_method", ValueKind::Option, 0, 0, {kAuthPassword, kAuthMlKem}},
        {"protocol.internal", "security_level", ValueKind::Option, 0, 0, security_options(true)},
        {"protocol.internal", "users_file", ValueKind::String},
        {"protocol.internal", "allow_anonymous", ValueKind::Bool},
        {"protocol.internal", "max_chunk_size", ValueKind::ChunkSize, kMinChunkSize, kMaxFrameSize},
        {"protocol.internal", "inflight_window_bytes", ValueKind::UInt64},
        {"protocol.internal", "batch_bytes", ValueKind::UInt64},
        {"protocol.internal", "batch_chunks", ValueKind::IntRange, 1, kMaxBatchChunks},
        {"protocol.internal", "preallocate_files", ValueKind::Bool},
        {"protocol.internal", "trusted_skip_zero_fill", ValueKind::Bool},
        {"protocol.internal", "cache_hints", ValueKind::Bool},
        {"protocol.internal", "streaming_verification", ValueKind::Bool},
        {"protocol.internal", "tcp_info_window", ValueKind::Bool},
        {"protocol.tls", "enable", ValueKind::Bool},
        {"protocol.tls", "tls_server_cert_file", ValueKind::String},
        {"protocol.tls", "tls_server_key_file", ValueKind::String},
        {"protocol.tls", "tls_dh_file", ValueKind::String},
        {"protocol.tls", "tls_client_cert_validation", ValueKind::Bool},
        {"protocol.tls", "tls_client_chain_validation", ValueKind::Bool},
        {"protocol.tls", "tls_trusted_chain_file", ValueKind::String},
        {"protocol.ssh", "enable", ValueKind::Bool},
        {"protocol.ssh", "port", ValueKind::IntRange, kMinPort, kMaxPort},
        {"protocol.sftp", "enable", ValueKind::Bool},
        {"logging", "enable", ValueKind::Bool},
        {"logging", "log_level", ValueKind::Option, 0, 0, log_level_options()},
        {"logging", "log_file", ValueKind::String},
        {"logging", "log_format", ValueKind::Option, 0, 0, {kLogFormatText, kLogFormatJson}},
        {"logging", "audit_file", ValueKind::String},
        {"console_output", "enable", ValueKind::Bool},
        {"console_output", "level", ValueKind::Option, 0, 0, log_level_options()},
        {"performance", "max_file_size", ValueKind::UInt64},
        {"performance", "max_bandwidth_percent", ValueKind::IntRange, 0, kMaxPercent},
        {"performance", "inflight_window_bytes", ValueKind::UInt64},
        {"performance", "batch_bytes", ValueKind::UInt64},
        {"performance", "batch_chunks", ValueKind::IntRange, 1, kMaxBatchChunks},
        {"performance", "preallocate_files", ValueKind::Bool},
        {"performance", "trusted_skip_zero_fill", ValueKind::Bool},
        {"performance", "cache_hints", ValueKind::Bool},
        {"performance", "streaming_verification", ValueKind::Bool},
        {"performance", "tcp_info_window", ValueKind::Bool},
        {"integration", "webhook_url", ValueKind::String},
        {"daemon", "run_as_daemon", ValueKind::Bool},
        {"daemon", "pid_file", ValueKind::String},
        {"paths", "allowed_paths", ValueKind::String},
        {"paths", "auto_create_directories", ValueKind::Bool},
        {"security", "secret_key", ValueKind::String},
        {"security", "require_auth", ValueKind::Bool},
        {"security", "tls", ValueKind::Bool},
        {"security", "tls_cert_file", ValueKind::String},
        {"security", "tls_key_file", ValueKind::String}
    };
}

std::vector<ConfigRule> client_rules() {
    using namespace defaults;
    return {
        {"connection", "timeout", ValueKind::IntRange, kMinPositiveValue, kMaxTimeoutSeconds},
        {"connection", "keep_alive", ValueKind::Bool},
        {"network", "udp", ValueKind::Bool},
        {"network", "socket_buffer_size", ValueKind::IntRange, 0, kMaxSocketBufferSize},
        {"protocol", "default_protocol", ValueKind::Option, 0, 0, {kProtocolInternal, kProtocolSsh, kProtocolSftp}},
        {"protocol.internal", "enable", ValueKind::Bool},
        {"protocol.internal", "secret_key", ValueKind::String},
        {"protocol.internal", "username", ValueKind::String},
        {"protocol.internal", "password", ValueKind::String},
        {"protocol.internal", "password_encrypted", ValueKind::String},
        {"protocol.internal", "auth_method", ValueKind::Option, 0, 0, {kAuthNone, kAuthPassword, kAuthMlKem}},
        {"protocol.internal", "security_level", ValueKind::Option, 0, 0, security_options(false)},
        {"protocol.internal", "private_key_file", ValueKind::String},
        {"protocol.internal", "private_key_passphrase", ValueKind::String},
        {"protocol.internal", "initial_chunk_size", ValueKind::IntRange, kMinChunkSize, kMaxFrameSize},
        {"protocol.internal", "min_chunk_size", ValueKind::IntRange, 1, kMaxFrameSize},
        {"protocol.internal", "max_chunk_size", ValueKind::ChunkSize, kMinChunkSize, kMaxFrameSize},
        {"protocol.internal", "chunk_size_increase_factor", ValueKind::DoubleRange, 1.0, 10.0},
        {"protocol.internal", "chunk_size_decrease_factor", ValueKind::DoubleRange, 0.01, 0.99},
        {"protocol.internal", "inflight_window_bytes", ValueKind::UInt64},
        {"protocol.internal", "batch_bytes", ValueKind::UInt64},
        {"protocol.internal", "batch_chunks", ValueKind::IntRange, 1, kMaxBatchChunks},
        {"protocol.internal", "preallocate_files", ValueKind::Bool},
        {"protocol.internal", "cache_hints", ValueKind::Bool},
        {"protocol.internal", "streaming_verification", ValueKind::Bool},
        {"protocol.internal", "tcp_info_window", ValueKind::Bool},
        {"protocol.tls", "enable", ValueKind::Bool},
        {"protocol.tls", "tls_mutual_authentication", ValueKind::Bool},
        {"protocol.tls", "tls_client_cert_file", ValueKind::String},
        {"protocol.tls", "tls_client_key_file", ValueKind::String},
        {"protocol.tls", "tls_server_cert_validation", ValueKind::Bool},
        {"protocol.tls", "tls_server_chain_validation", ValueKind::Bool},
        {"protocol.tls", "tls_trusted_chain_file", ValueKind::String},
        {"protocol.ssh", "enable", ValueKind::Bool},
        {"protocol.ssh", "username", ValueKind::String},
        {"protocol.ssh", "private_key_file", ValueKind::String},
        {"protocol.sftp", "enable", ValueKind::Bool},
        {"performance", "max_bandwidth_percent", ValueKind::IntRange, 0, kMaxPercent},
        {"performance", "retry_attempts", ValueKind::IntRange, 0, kMaxRetryAttempts},
        {"performance", "retry_delay", ValueKind::IntRange, 0, kMaxTimeoutSeconds},
        {"performance", "inflight_window_bytes", ValueKind::UInt64},
        {"performance", "batch_bytes", ValueKind::UInt64},
        {"performance", "batch_chunks", ValueKind::IntRange, 1, kMaxBatchChunks},
        {"performance", "preallocate_files", ValueKind::Bool},
        {"performance", "cache_hints", ValueKind::Bool},
        {"performance", "streaming_verification", ValueKind::Bool},
        {"performance", "tcp_info_window", ValueKind::Bool},
        {"logging", "enable", ValueKind::Bool},
        {"logging", "log_level", ValueKind::Option, 0, 0, log_level_options()},
        {"logging", "log_file", ValueKind::String},
        {"logging", "log_format", ValueKind::Option, 0, 0, {kLogFormatText, kLogFormatJson}},
        {"console_output", "enable", ValueKind::Bool},
        {"console_output", "level", ValueKind::Option, 0, 0, log_level_options()},
        {"transfer", "create_empty_directories", ValueKind::Bool},
        {"transfer", "auto_create_directories", ValueKind::Bool},
        {"proxy", "type", ValueKind::Option, 0, 0, {kProxyNone, kProxySocks5, kProxyHttp}},
        {"proxy", "host", ValueKind::String},
        {"proxy", "port", ValueKind::IntRange, 0, kMaxPort},
        {"proxy", "username", ValueKind::String},
        {"proxy", "password", ValueKind::String},
        {"integration", "webhook_url", ValueKind::String},
        {"gui", "port", ValueKind::IntRange, kMinPort, kMaxPort},
        {"gui", "open_browser_on_start", ValueKind::Bool},
        {"gui", "theme", ValueKind::Option, 0, 0, {kGuiThemeSystem, kGuiThemeLight, kGuiThemeDark}},
        {"gui", "language", ValueKind::String},
        {"security", "secret_key", ValueKind::String},
        {"security", "tls", ValueKind::Bool},
        {"security", "tls_verify", ValueKind::Bool},
        {"security", "tls_ca_file", ValueKind::String},
        {"auth", "username", ValueKind::String},
        {"auth", "password", ValueKind::String},
        {"auth", "auth_method", ValueKind::Option, 0, 0, {kAuthNone, kAuthPassword, kAuthMlKem}},
        {"auth", "private_key_file", ValueKind::String}
    };
}

void validate_value(const ConfigRule& rule, const std::string& value, int line, std::vector<ConfigValidationIssue>& issues) {
    int64_t int_value = 0;
    double double_value = 0.0;
    switch (rule.kind) {
        case ValueKind::String:
            if (std::string(rule.key) == "secret_key" && !is_hex_secret_key(value)) {
                issues.push_back({line, std::string(rule.section) + "." + rule.key + " must be empty or a 32-byte hex string (64 hex characters, optional 0x prefix)"});
            }
            break;
        case ValueKind::Bool:
            if (!is_valid_bool(value)) {
                issues.push_back({line, std::string(rule.section) + "." + rule.key + " must be a boolean: true/false, yes/no, on/off, or 1/0"});
            }
            break;
        case ValueKind::IntRange:
            if (!parse_int64(value, int_value) || int_value < rule.min_value || int_value > rule.max_value) {
                issues.push_back({line, std::string(rule.section) + "." + rule.key + " must be an integer in range " +
                    std::to_string(static_cast<int64_t>(rule.min_value)) + "-" + std::to_string(static_cast<int64_t>(rule.max_value))});
            }
            break;
        case ValueKind::UInt64:
            if (!parse_int64(value, int_value) || int_value < 0) {
                issues.push_back({line, std::string(rule.section) + "." + rule.key + " must be a non-negative integer"});
            }
            break;
        case ValueKind::DoubleRange:
            if (!parse_double_value(value, double_value) || double_value < rule.min_value || double_value > rule.max_value) {
                issues.push_back({line, std::string(rule.section) + "." + rule.key + " must be a number in range " +
                    std::to_string(rule.min_value) + "-" + std::to_string(rule.max_value)});
            }
            break;
        case ValueKind::Option:
            if (!is_one_of(value, rule.options)) {
                std::ostringstream msg;
                msg << rule.section << "." << rule.key << " must be one of: ";
                for (size_t i = 0; i < rule.options.size(); ++i) {
                    if (i != 0) msg << ", ";
                    msg << rule.options[i];
                }
                issues.push_back({line, msg.str()});
            }
            break;
        case ValueKind::ChunkSize:
            if (is_one_of(value, {"adaptive", "automatic"})) {
                break;
            }
            if (!parse_int64(value, int_value) || int_value < rule.min_value || int_value > rule.max_value) {
                issues.push_back({line, std::string(rule.section) + "." + rule.key + " must be 'adaptive' or an integer in range " +
                    std::to_string(static_cast<int64_t>(rule.min_value)) + "-" + std::to_string(static_cast<int64_t>(rule.max_value))});
            }
            break;
    }
}

std::vector<ConfigValidationIssue> validate_file_with_rules(const std::string& filename, const std::vector<ConfigRule>& rules) {
    std::ifstream file(std::filesystem::u8path(filename));
    if (!file) {
        return {{0, "Failed to open config file: " + filename}};
    }

    auto rule_map = make_rule_map(rules);
    std::vector<ConfigValidationIssue> issues;
    std::string current_section;
    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        ++line_number;
        std::string trimmed = trim_copy(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        if (trimmed[0] == '[') {
            if (trimmed.back() != ']') {
                issues.push_back({line_number, "Malformed section header. Expected [section]"});
                continue;
            }
            current_section = trim_copy(trimmed.substr(1, trimmed.size() - 2));
            if (current_section.empty()) {
                issues.push_back({line_number, "Section name cannot be empty"});
            }
            continue;
        }

        size_t equals_pos = trimmed.find('=');
        if (equals_pos == std::string::npos) {
            issues.push_back({line_number, "Malformed key/value line. Expected key = value"});
            continue;
        }
        if (current_section.empty()) {
            issues.push_back({line_number, "Key/value pair appears before any section header"});
            continue;
        }

        std::string key = trim_copy(trimmed.substr(0, equals_pos));
        std::string value = strip_quotes(trimmed.substr(equals_pos + 1));
        if (key.empty()) {
            issues.push_back({line_number, "Configuration key cannot be empty"});
            continue;
        }

        auto rule_it = rule_map.find(rule_key(current_section, key));
        if (rule_it != rule_map.end()) {
            validate_value(rule_it->second, value, line_number, issues);
        }
    }

    return issues;
}

std::string bool_string(bool value) {
    return value ? "true" : "false";
}

uint64_t get_uint64_prefer(const ConfigParser& parser,
                           const std::string& primary_section,
                           const std::string& primary_key,
                           const std::string& legacy_section,
                           const std::string& legacy_key,
                           uint64_t default_value) {
    if (parser.has_key(primary_section, primary_key)) {
        return parser.get_uint64(primary_section, primary_key, default_value);
    }
    return parser.get_uint64(legacy_section, legacy_key, default_value);
}

int get_int_prefer(const ConfigParser& parser,
                   const std::string& primary_section,
                   const std::string& primary_key,
                   const std::string& legacy_section,
                   const std::string& legacy_key,
                   int default_value) {
    if (parser.has_key(primary_section, primary_key)) {
        return parser.get_int(primary_section, primary_key, default_value);
    }
    return parser.get_int(legacy_section, legacy_key, default_value);
}

bool get_bool_prefer(const ConfigParser& parser,
                     const std::string& primary_section,
                     const std::string& primary_key,
                     const std::string& legacy_section,
                     const std::string& legacy_key,
                     bool default_value) {
    if (parser.has_key(primary_section, primary_key)) {
        return parser.get_bool(primary_section, primary_key, default_value);
    }
    return parser.get_bool(legacy_section, legacy_key, default_value);
}

} // namespace

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
    if (value == "true" || value == "yes" || value == "1" || value == "on") {
        return true;
    }
    if (value == "false" || value == "no" || value == "0" || value == "off") {
        return false;
    }
    return default_value;
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

std::string ConfigParser::format_validation_issues(const std::vector<ConfigValidationIssue>& issues) {
    std::ostringstream stream;
    for (const auto& issue : issues) {
        if (issue.line > 0) {
            stream << "line " << issue.line << ": ";
        }
        stream << issue.message << "\n";
    }
    return stream.str();
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
    auto issues = validate_file(filename);
    if (!issues.empty()) {
        throw ConfigException("Invalid server configuration '" + filename + "':\n" +
                              ConfigParser::format_validation_issues(issues));
    }

    ConfigParser parser;
    parser.load_from_file(filename);

    ServerConfig config = get_default();
    config.listen_address = parser.get_string("network", "listen_address", config.listen_address);
    config.listen_port = static_cast<uint16_t>(parser.get_int("network", "listen_port", config.listen_port));
    config.max_connections = parser.get_int("network", "max_connections", config.max_connections);
    config.timeout = parser.get_int("network", "timeout", config.timeout);
    config.udp = parser.get_bool("network", "udp", config.udp);
    config.socket_buffer_size = parser.get_int("network", "socket_buffer_size", config.socket_buffer_size);
    
    config.default_protocol = parser.get_string("protocol", "default_protocol", config.default_protocol);
    
    // Protocol Internal
    config.internal.enable = parser.get_bool("protocol.internal", "enable", config.internal.enable);
    config.internal.secret_key = parser.get_string("protocol.internal", "secret_key", config.internal.secret_key);
    config.internal.require_auth = parser.get_bool("protocol.internal", "require_auth", config.internal.require_auth);
    config.internal.auth_method = parser.get_string("protocol.internal", "auth_method", config.internal.auth_method);
    config.internal.security_level = parser.get_string("protocol.internal", "security_level", config.internal.security_level);
    config.internal.users_file = parser.get_string("protocol.internal", "users_file", config.internal.users_file);
    config.internal.allow_anonymous = parser.get_bool("protocol.internal", "allow_anonymous", config.internal.allow_anonymous);
    
    std::string max_chunk_str = parser.get_string("protocol.internal", "max_chunk_size", "adaptive");
    std::string max_chunk_normalized = lower_copy(max_chunk_str);
    if (max_chunk_normalized == "adaptive" || max_chunk_normalized == "automatic") {
        config.internal.adaptive_chunk_size = true;
        config.internal.max_chunk_size = defaults::kMaxChunkSize;
    } else {
        config.internal.adaptive_chunk_size = false;
        config.internal.max_chunk_size = static_cast<size_t>(std::stoull(max_chunk_str.empty() ? std::to_string(defaults::kMaxChunkSize) : max_chunk_str));
    }
    config.internal.inflight_window_bytes = get_uint64_prefer(parser, "protocol.internal", "inflight_window_bytes", "performance", "inflight_window_bytes", config.internal.inflight_window_bytes);
    config.internal.batch_bytes = get_uint64_prefer(parser, "protocol.internal", "batch_bytes", "performance", "batch_bytes", config.internal.batch_bytes);
    config.internal.batch_chunks = get_int_prefer(parser, "protocol.internal", "batch_chunks", "performance", "batch_chunks", config.internal.batch_chunks);
    config.internal.preallocate_files = get_bool_prefer(parser, "protocol.internal", "preallocate_files", "performance", "preallocate_files", config.internal.preallocate_files);
    config.internal.trusted_skip_zero_fill = get_bool_prefer(parser, "protocol.internal", "trusted_skip_zero_fill", "performance", "trusted_skip_zero_fill", config.internal.trusted_skip_zero_fill);
    config.internal.cache_hints = get_bool_prefer(parser, "protocol.internal", "cache_hints", "performance", "cache_hints", config.internal.cache_hints);
    config.internal.streaming_verification = get_bool_prefer(parser, "protocol.internal", "streaming_verification", "performance", "streaming_verification", config.internal.streaming_verification);
    config.internal.tcp_info_window = get_bool_prefer(parser, "protocol.internal", "tcp_info_window", "performance", "tcp_info_window", config.internal.tcp_info_window);
    
    // Protocol TLS
    config.tls.enable = parser.get_bool("protocol.tls", "enable", config.tls.enable);
    config.tls.server_cert_file = parser.get_string("protocol.tls", "tls_server_cert_file", config.tls.server_cert_file);
    config.tls.server_key_file = parser.get_string("protocol.tls", "tls_server_key_file", config.tls.server_key_file);
    config.tls.dh_file = parser.get_string("protocol.tls", "tls_dh_file", config.tls.dh_file);
    config.tls.client_cert_validation = parser.get_bool("protocol.tls", "tls_client_cert_validation", config.tls.client_cert_validation);
    config.tls.client_chain_validation = parser.get_bool("protocol.tls", "tls_client_chain_validation", config.tls.client_chain_validation);
    config.tls.trusted_chain_file = parser.get_string("protocol.tls", "tls_trusted_chain_file", config.tls.trusted_chain_file);
    
    // Protocol SSH
    config.ssh.enable = parser.get_bool("protocol.ssh", "enable", config.ssh.enable);
    config.ssh.port = static_cast<uint16_t>(parser.get_int("protocol.ssh", "port", config.ssh.port));
    
    // Protocol SFTP
    config.sftp.enable = parser.get_bool("protocol.sftp", "enable", config.sftp.enable);
    
    // Logging
    config.logging.enable = parser.get_bool("logging", "enable", config.logging.enable);
    config.logging.level = parser.get_string("logging", "log_level", config.logging.level);
    config.logging.file = parser.get_string("logging", "log_file", config.logging.file);
    config.logging.format = parser.get_string("logging", "log_format", config.logging.format);
    config.logging.audit_file = parser.get_string("logging", "audit_file", config.logging.audit_file);
    
    // Console
    config.console.enable = parser.get_bool("console_output", "enable", config.console.enable);
    config.console.level = parser.get_string("console_output", "level", config.console.level);
    
    config.max_file_size = parser.get_uint64("performance", "max_file_size", config.max_file_size);
    config.max_bandwidth_percent = parser.get_int("performance", "max_bandwidth_percent", config.max_bandwidth_percent);
    
    config.webhook_url = parser.get_string("integration", "webhook_url", config.webhook_url);
    
    config.run_as_daemon = parser.get_bool("daemon", "run_as_daemon", config.run_as_daemon);
    config.pid_file = parser.get_string("daemon", "pid_file", config.pid_file);
    
    config.allowed_paths = parser.get_string_list("paths", "allowed_paths", config.allowed_paths);
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
    config.auto_create_directories = parser.get_bool("paths", "auto_create_directories", config.auto_create_directories);
    
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
    using namespace defaults;
    ServerConfig config;
    config.listen_address = kServerListenAddress;
    config.listen_port = kDefaultTransferPort;
    config.max_connections = kServerMaxConnections;
    config.timeout = kDefaultTimeoutSeconds;
    config.udp = kServerUdpEnabled;
    config.socket_buffer_size = kDefaultSocketBufferSize;
    
    config.default_protocol = kProtocolInternal;
    
    config.internal.enable = kServerInternalEnabled;
    config.internal.secret_key = "";
    config.internal.require_auth = kServerRequireAuth;
    config.internal.auth_method = kAuthPassword;
    config.internal.security_level = kSecurityAuto;
    config.internal.users_file = kServerUsersFile;
    config.internal.allow_anonymous = kServerAllowAnonymous;
    config.internal.max_chunk_size = kMaxChunkSize;
    config.internal.adaptive_chunk_size = kServerAdaptiveChunkSize;
    config.internal.inflight_window_bytes = kDefaultInflightWindowBytes;
    config.internal.batch_bytes = kDefaultBatchBytes;
    config.internal.batch_chunks = kDefaultBatchChunks;
    config.internal.preallocate_files = kDefaultPreallocateFiles;
    config.internal.trusted_skip_zero_fill = kDefaultTrustedSkipZeroFill;
    config.internal.cache_hints = kDefaultCacheHints;
    config.internal.streaming_verification = kDefaultStreamingVerification;
    config.internal.tcp_info_window = kDefaultTcpInfoWindow;

    config.tls.enable = kServerTlsEnabled;
    config.tls.server_cert_file = "";
    config.tls.server_key_file = "";
    config.tls.dh_file = "";
    config.tls.client_cert_validation = kServerTlsClientCertValidation;
    config.tls.client_chain_validation = kServerTlsClientChainValidation;
    config.tls.trusted_chain_file = "";
    
    config.ssh.enable = kServerSshEnabled;
    config.ssh.port = kServerSshPort;
    
    config.sftp.enable = kServerSftpEnabled;
    
    config.logging.enable = kServerLoggingEnabled;
    config.logging.level = kLogLevelInfo;
    config.logging.file = kServerLogFile;
    config.logging.format = kLogFormatText;
    config.logging.audit_file = kServerAuditFile;
    
    config.console.enable = kServerConsoleEnabled;
    config.console.level = kLogLevelInfo;
    
    config.max_file_size = kUnlimitedFileSize;
    config.max_bandwidth_percent = kDefaultMaxBandwidthPercent;
    config.webhook_url = "";
    config.run_as_daemon = kServerRunAsDaemon;
    config.pid_file = kServerPidFile;
    config.allowed_paths = {kServerAllowedPath};
    config.auto_create_directories = kServerAutoCreateDirectories;
    
    return config;
}

void ServerConfig::create_default_file(const std::string& filename) {
    ServerConfig config = get_default();
    std::ostringstream stream;
    stream << "# NetCopy server configuration\n";
    stream << "[network]\n";
    stream << "listen_address = " << config.listen_address << "\n";
    stream << "listen_port = " << config.listen_port << "\n";
    stream << "max_connections = " << config.max_connections << "\n";
    stream << "timeout = " << config.timeout << "\n";
    stream << "udp = " << bool_string(config.udp) << "\n";
    stream << "socket_buffer_size = " << config.socket_buffer_size << "\n\n";
    stream << "[protocol]\n";
    stream << "default_protocol = " << config.default_protocol << "\n\n";
    stream << "[protocol.internal]\n";
    stream << "enable = " << bool_string(config.internal.enable) << "\n";
    stream << "secret_key = " << config.internal.secret_key << "\n";
    stream << "require_auth = " << bool_string(config.internal.require_auth) << "\n";
    stream << "auth_method = " << config.internal.auth_method << "\n";
    stream << "security_level = " << config.internal.security_level << "\n";
    stream << "users_file = " << config.internal.users_file << "\n";
    stream << "allow_anonymous = " << bool_string(config.internal.allow_anonymous) << "\n";
    stream << "max_chunk_size = adaptive\n";
    stream << "inflight_window_bytes = " << config.internal.inflight_window_bytes << "\n";
    stream << "batch_bytes = " << config.internal.batch_bytes << "\n";
    stream << "batch_chunks = " << config.internal.batch_chunks << "\n";
    stream << "preallocate_files = " << bool_string(config.internal.preallocate_files) << "\n";
    stream << "trusted_skip_zero_fill = " << bool_string(config.internal.trusted_skip_zero_fill) << "\n";
    stream << "cache_hints = " << bool_string(config.internal.cache_hints) << "\n";
    stream << "streaming_verification = " << bool_string(config.internal.streaming_verification) << "\n";
    stream << "tcp_info_window = " << bool_string(config.internal.tcp_info_window) << "\n\n";
    stream << "[protocol.tls]\n";
    stream << "enable = " << bool_string(config.tls.enable) << "\n";
    stream << "tls_server_cert_file = " << config.tls.server_cert_file << "\n";
    stream << "tls_server_key_file = " << config.tls.server_key_file << "\n";
    stream << "tls_dh_file = " << config.tls.dh_file << "\n";
    stream << "tls_client_cert_validation = " << bool_string(config.tls.client_cert_validation) << "\n";
    stream << "tls_client_chain_validation = " << bool_string(config.tls.client_chain_validation) << "\n";
    stream << "tls_trusted_chain_file = " << config.tls.trusted_chain_file << "\n\n";
    stream << "[protocol.ssh]\n";
    stream << "enable = " << bool_string(config.ssh.enable) << "\n";
    stream << "port = " << config.ssh.port << "\n\n";
    stream << "[protocol.sftp]\n";
    stream << "enable = " << bool_string(config.sftp.enable) << "\n\n";
    stream << "[logging]\n";
    stream << "enable = " << bool_string(config.logging.enable) << "\n";
    stream << "log_level = " << config.logging.level << "\n";
    stream << "log_file = " << config.logging.file << "\n";
    stream << "log_format = " << config.logging.format << "\n";
    stream << "audit_file = " << config.logging.audit_file << "\n\n";
    stream << "[console_output]\n";
    stream << "enable = " << bool_string(config.console.enable) << "\n";
    stream << "level = " << config.console.level << "\n\n";
    stream << "[performance]\n";
    stream << "max_file_size = " << config.max_file_size << "\n";
    stream << "max_bandwidth_percent = " << config.max_bandwidth_percent << "\n\n";
    stream << "[integration]\n";
    stream << "webhook_url = " << config.webhook_url << "\n\n";
    stream << "[daemon]\n";
    stream << "run_as_daemon = " << bool_string(config.run_as_daemon) << "\n";
    stream << "pid_file = " << config.pid_file << "\n\n";
    stream << "[paths]\n";
    for (const auto& path : config.allowed_paths) {
        stream << "allowed_paths = " << path << "\n";
    }
    stream << "auto_create_directories = " << bool_string(config.auto_create_directories) << "\n";

    std::ofstream file(std::filesystem::u8path(filename));
    if (!file) {
        throw ConfigException("Failed to create default server config file: " + filename);
    }
    file << stream.str();
}

std::vector<ConfigValidationIssue> ServerConfig::validate_file(const std::string& filename) {
    return validate_file_with_rules(filename, server_rules());
}

// ClientConfig implementation
ClientConfig ClientConfig::load_from_file(const std::string& filename) {
    auto issues = validate_file(filename);
    if (!issues.empty()) {
        throw ConfigException("Invalid client configuration '" + filename + "':\n" +
                              ConfigParser::format_validation_issues(issues));
    }

    ConfigParser parser;
    parser.load_from_file(filename);
    
    ClientConfig config = get_default();
    config.timeout = parser.get_int("connection", "timeout", config.timeout);
    config.keep_alive = parser.get_bool("connection", "keep_alive", config.keep_alive);
    config.udp = parser.get_bool("network", "udp", config.udp);
    config.socket_buffer_size = parser.get_int("network", "socket_buffer_size", config.socket_buffer_size);
    
    config.default_protocol = parser.get_string("protocol", "default_protocol", config.default_protocol);
    
    // Protocol Internal
    config.internal.enable = parser.get_bool("protocol.internal", "enable", config.internal.enable);
    config.internal.secret_key = parser.get_string("protocol.internal", "secret_key", config.internal.secret_key);
    config.internal.username = parser.get_string("protocol.internal", "username", config.internal.username);
    config.internal.password = parser.get_string("protocol.internal", "password", config.internal.password);
    config.internal.password_encrypted = parser.get_string("protocol.internal", "password_encrypted", config.internal.password_encrypted);
    config.internal.auth_method = parser.get_string("protocol.internal", "auth_method", config.internal.auth_method);
    config.internal.security_level = parser.get_string("protocol.internal", "security_level", config.internal.security_level);
    config.internal.private_key_file = parser.get_string("protocol.internal", "private_key_file", config.internal.private_key_file);
    config.internal.private_key_passphrase = parser.get_string("protocol.internal", "private_key_passphrase", config.internal.private_key_passphrase);
    config.internal.initial_chunk_size = static_cast<size_t>(parser.get_int("protocol.internal", "initial_chunk_size", static_cast<int>(config.internal.initial_chunk_size)));
    config.internal.min_chunk_size = static_cast<size_t>(parser.get_int("protocol.internal", "min_chunk_size", static_cast<int>(config.internal.min_chunk_size)));
    std::string max_chunk_str = parser.get_string("protocol.internal", "max_chunk_size", "adaptive");
    std::string max_chunk_normalized = lower_copy(max_chunk_str);
    if (max_chunk_normalized == "adaptive" || max_chunk_normalized == "automatic") {
        config.internal.max_chunk_size = defaults::kMaxChunkSize;
    } else {
        config.internal.max_chunk_size = static_cast<size_t>(std::stoull(max_chunk_str.empty() ? std::to_string(defaults::kMaxChunkSize) : max_chunk_str));
    }
    
    config.internal.chunk_size_increase_factor = std::stod(parser.get_string("protocol.internal", "chunk_size_increase_factor", std::to_string(config.internal.chunk_size_increase_factor)));
    config.internal.chunk_size_decrease_factor = std::stod(parser.get_string("protocol.internal", "chunk_size_decrease_factor", std::to_string(config.internal.chunk_size_decrease_factor)));
    config.internal.inflight_window_bytes = get_uint64_prefer(parser, "protocol.internal", "inflight_window_bytes", "performance", "inflight_window_bytes", config.internal.inflight_window_bytes);
    config.internal.batch_bytes = get_uint64_prefer(parser, "protocol.internal", "batch_bytes", "performance", "batch_bytes", config.internal.batch_bytes);
    config.internal.batch_chunks = get_int_prefer(parser, "protocol.internal", "batch_chunks", "performance", "batch_chunks", config.internal.batch_chunks);
    config.internal.preallocate_files = get_bool_prefer(parser, "protocol.internal", "preallocate_files", "performance", "preallocate_files", config.internal.preallocate_files);
    config.internal.cache_hints = get_bool_prefer(parser, "protocol.internal", "cache_hints", "performance", "cache_hints", config.internal.cache_hints);
    config.internal.streaming_verification = get_bool_prefer(parser, "protocol.internal", "streaming_verification", "performance", "streaming_verification", config.internal.streaming_verification);
    config.internal.tcp_info_window = get_bool_prefer(parser, "protocol.internal", "tcp_info_window", "performance", "tcp_info_window", config.internal.tcp_info_window);
    
    // Protocol TLS
    config.tls.enable = parser.get_bool("protocol.tls", "enable", config.tls.enable);
    config.tls.mutual_authentication = parser.get_bool("protocol.tls", "tls_mutual_authentication", config.tls.mutual_authentication);
    config.tls.client_cert_file = parser.get_string("protocol.tls", "tls_client_cert_file", config.tls.client_cert_file);
    config.tls.client_key_file = parser.get_string("protocol.tls", "tls_client_key_file", config.tls.client_key_file);
    config.tls.server_cert_validation = parser.get_bool("protocol.tls", "tls_server_cert_validation", config.tls.server_cert_validation);
    config.tls.server_chain_validation = parser.get_bool("protocol.tls", "tls_server_chain_validation", config.tls.server_chain_validation);
    config.tls.trusted_chain_file = parser.get_string("protocol.tls", "tls_trusted_chain_file", config.tls.trusted_chain_file);
    
    // Protocol SSH
    config.ssh.enable = parser.get_bool("protocol.ssh", "enable", config.ssh.enable);
    config.ssh.username = parser.get_string("protocol.ssh", "username", config.ssh.username);
    config.ssh.private_key_file = parser.get_string("protocol.ssh", "private_key_file", config.ssh.private_key_file);
    
    // Protocol SFTP
    config.sftp.enable = parser.get_bool("protocol.sftp", "enable", config.sftp.enable);
    
    // Performance
    config.max_bandwidth_percent = parser.get_int("performance", "max_bandwidth_percent", config.max_bandwidth_percent);
    config.retry_attempts = parser.get_int("performance", "retry_attempts", config.retry_attempts);
    config.retry_delay = parser.get_int("performance", "retry_delay", config.retry_delay);
    
    // Logging
    config.logging.enable = parser.get_bool("logging", "enable", config.logging.enable);
    config.logging.level = parser.get_string("logging", "log_level", config.logging.level);
    config.logging.file = parser.get_string("logging", "log_file", config.logging.file);
    config.logging.format = parser.get_string("logging", "log_format", config.logging.format);
    
    // Console
    config.console.enable = parser.get_bool("console_output", "enable", config.console.enable);
    config.console.level = parser.get_string("console_output", "level", config.console.level);
    
    config.create_empty_directories = parser.get_bool("transfer", "create_empty_directories", config.create_empty_directories);
    config.auto_create_directories = parser.get_bool("transfer", "auto_create_directories", config.auto_create_directories);
    
    config.proxy_type = parser.get_string("proxy", "type", config.proxy_type);
    config.proxy_host = parser.get_string("proxy", "host", config.proxy_host);
    config.proxy_port = static_cast<uint16_t>(parser.get_int("proxy", "port", config.proxy_port));
    config.proxy_username = parser.get_string("proxy", "username", config.proxy_username);
    config.proxy_password = parser.get_string("proxy", "password", config.proxy_password);
    config.webhook_url = parser.get_string("integration", "webhook_url", config.webhook_url);
    
    // GUI settings
    config.gui.port = static_cast<uint16_t>(parser.get_int("gui", "port", config.gui.port));
    config.gui.open_browser_on_start = parser.get_bool("gui", "open_browser_on_start", config.gui.open_browser_on_start);
    config.gui.theme = parser.get_string("gui", "theme", config.gui.theme);
    config.gui.language = parser.get_string("gui", "language", config.gui.language);
    
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
    using namespace defaults;
    ClientConfig config;
    config.timeout = kDefaultTimeoutSeconds;
    config.keep_alive = kClientKeepAlive;
    config.udp = kClientUdpEnabled;
    config.socket_buffer_size = kDefaultSocketBufferSize;
    
    config.default_protocol = kProtocolInternal;
    
    config.internal.enable = kClientInternalEnabled;
    config.internal.secret_key = "";
    config.internal.username = "";
    config.internal.password = "";
    config.internal.password_encrypted = "";
    config.internal.auth_method = kAuthNone;
    config.internal.security_level = kSecurityHigh;
    config.internal.private_key_file = "";
    config.internal.private_key_passphrase = "";
    config.internal.initial_chunk_size = kInitialChunkSize;
    config.internal.min_chunk_size = kMinChunkSize;
    config.internal.max_chunk_size = kMaxChunkSize;
    config.internal.chunk_size_increase_factor = kChunkSizeIncreaseFactor;
    config.internal.chunk_size_decrease_factor = kChunkSizeDecreaseFactor;
    config.internal.inflight_window_bytes = kDefaultInflightWindowBytes;
    config.internal.batch_bytes = kDefaultBatchBytes;
    config.internal.batch_chunks = kDefaultBatchChunks;
    config.internal.preallocate_files = kDefaultPreallocateFiles;
    config.internal.cache_hints = kDefaultCacheHints;
    config.internal.streaming_verification = kDefaultStreamingVerification;
    config.internal.tcp_info_window = kDefaultTcpInfoWindow;

    config.tls.enable = kClientTlsEnabled;
    config.tls.mutual_authentication = kClientTlsMutualAuthentication;
    config.tls.client_cert_file = "";
    config.tls.client_key_file = "";
    config.tls.server_cert_validation = kClientTlsServerCertValidation;
    config.tls.server_chain_validation = kClientTlsServerChainValidation;
    config.tls.trusted_chain_file = "";
    
    config.ssh.enable = kClientSshEnabled;
    config.ssh.username = "";
    config.ssh.private_key_file = "";
    
    config.sftp.enable = kClientSftpEnabled;
    
    config.max_bandwidth_percent = kDefaultMaxBandwidthPercent;
    config.retry_attempts = kClientRetryAttempts;
    config.retry_delay = kClientRetryDelaySeconds;
    
    config.logging.enable = kClientLoggingEnabled;
    config.logging.level = kLogLevelInfo;
    config.logging.file = kClientLogFile;
    config.logging.format = kLogFormatText;
    
    config.console.enable = kClientConsoleEnabled;
    config.console.level = kLogLevelInfo;
    
    config.create_empty_directories = kCreateEmptyDirectories;
    config.auto_create_directories = kAutoCreateDirectories;
    
    config.proxy_type = kProxyNone;
    config.proxy_host = "";
    config.proxy_port = 0;
    config.proxy_username = "";
    config.proxy_password = "";
    config.webhook_url = "";
    config.gui.port = kGuiPort;
    config.gui.open_browser_on_start = kGuiOpenBrowserOnStart;
    config.gui.theme = kGuiThemeSystem;
    config.gui.language = kGuiLanguage;
    
    return config;
}

void ClientConfig::create_default_file(const std::string& filename) {
    ClientConfig config = get_default();
    std::ostringstream stream;
    stream << "# NetCopy client and GUI configuration\n";
    stream << "[connection]\n";
    stream << "timeout = " << config.timeout << "\n";
    stream << "keep_alive = " << bool_string(config.keep_alive) << "\n\n";
    stream << "[network]\n";
    stream << "udp = " << bool_string(config.udp) << "\n";
    stream << "socket_buffer_size = " << config.socket_buffer_size << "\n\n";
    stream << "[protocol]\n";
    stream << "default_protocol = " << config.default_protocol << "\n\n";
    stream << "[protocol.internal]\n";
    stream << "enable = " << bool_string(config.internal.enable) << "\n";
    stream << "secret_key = " << config.internal.secret_key << "\n";
    stream << "username = " << config.internal.username << "\n";
    stream << "password = " << config.internal.password << "\n";
    stream << "password_encrypted = " << config.internal.password_encrypted << "\n";
    stream << "auth_method = " << config.internal.auth_method << "\n";
    stream << "security_level = " << config.internal.security_level << "\n";
    stream << "private_key_file = " << config.internal.private_key_file << "\n";
    stream << "private_key_passphrase = " << config.internal.private_key_passphrase << "\n";
    stream << "initial_chunk_size = " << config.internal.initial_chunk_size << "\n";
    stream << "min_chunk_size = " << config.internal.min_chunk_size << "\n";
    stream << "max_chunk_size = adaptive\n";
    stream << "chunk_size_increase_factor = " << config.internal.chunk_size_increase_factor << "\n";
    stream << "chunk_size_decrease_factor = " << config.internal.chunk_size_decrease_factor << "\n";
    stream << "inflight_window_bytes = " << config.internal.inflight_window_bytes << "\n";
    stream << "batch_bytes = " << config.internal.batch_bytes << "\n";
    stream << "batch_chunks = " << config.internal.batch_chunks << "\n";
    stream << "preallocate_files = " << bool_string(config.internal.preallocate_files) << "\n";
    stream << "cache_hints = " << bool_string(config.internal.cache_hints) << "\n";
    stream << "streaming_verification = " << bool_string(config.internal.streaming_verification) << "\n";
    stream << "tcp_info_window = " << bool_string(config.internal.tcp_info_window) << "\n\n";
    stream << "[protocol.tls]\n";
    stream << "enable = " << bool_string(config.tls.enable) << "\n";
    stream << "tls_mutual_authentication = " << bool_string(config.tls.mutual_authentication) << "\n";
    stream << "tls_client_cert_file = " << config.tls.client_cert_file << "\n";
    stream << "tls_client_key_file = " << config.tls.client_key_file << "\n";
    stream << "tls_server_cert_validation = " << bool_string(config.tls.server_cert_validation) << "\n";
    stream << "tls_server_chain_validation = " << bool_string(config.tls.server_chain_validation) << "\n";
    stream << "tls_trusted_chain_file = " << config.tls.trusted_chain_file << "\n\n";
    stream << "[protocol.ssh]\n";
    stream << "enable = " << bool_string(config.ssh.enable) << "\n";
    stream << "username = " << config.ssh.username << "\n";
    stream << "private_key_file = " << config.ssh.private_key_file << "\n\n";
    stream << "[protocol.sftp]\n";
    stream << "enable = " << bool_string(config.sftp.enable) << "\n\n";
    stream << "[performance]\n";
    stream << "max_bandwidth_percent = " << config.max_bandwidth_percent << "\n";
    stream << "retry_attempts = " << config.retry_attempts << "\n";
    stream << "retry_delay = " << config.retry_delay << "\n\n";
    stream << "[logging]\n";
    stream << "enable = " << bool_string(config.logging.enable) << "\n";
    stream << "log_level = " << config.logging.level << "\n";
    stream << "log_file = " << config.logging.file << "\n";
    stream << "log_format = " << config.logging.format << "\n\n";
    stream << "[console_output]\n";
    stream << "enable = " << bool_string(config.console.enable) << "\n";
    stream << "level = " << config.console.level << "\n\n";
    stream << "[transfer]\n";
    stream << "create_empty_directories = " << bool_string(config.create_empty_directories) << "\n";
    stream << "auto_create_directories = " << bool_string(config.auto_create_directories) << "\n\n";
    stream << "[proxy]\n";
    stream << "type = " << config.proxy_type << "\n";
    stream << "host = " << config.proxy_host << "\n";
    stream << "port = " << config.proxy_port << "\n";
    stream << "username = " << config.proxy_username << "\n";
    stream << "password = " << config.proxy_password << "\n\n";
    stream << "[integration]\n";
    stream << "webhook_url = " << config.webhook_url << "\n\n";
    stream << "[gui]\n";
    stream << "port = " << config.gui.port << "\n";
    stream << "open_browser_on_start = " << bool_string(config.gui.open_browser_on_start) << "\n";
    stream << "theme = " << config.gui.theme << "\n";
    stream << "language = " << config.gui.language << "\n";

    std::ofstream file(std::filesystem::u8path(filename));
    if (!file) {
        throw ConfigException("Failed to create default client config file: " + filename);
    }
    file << stream.str();
}

std::vector<ConfigValidationIssue> ClientConfig::validate_file(const std::string& filename) {
    return validate_file_with_rules(filename, client_rules());
}

} // namespace config
} // namespace netcopy

