#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace netcopy {
namespace common {

// String utilities
std::string to_hex_string(const std::vector<uint8_t>& data);
std::vector<uint8_t> from_hex_string(const std::string& hex_str);

// Path utilities
std::string get_executable_path();
std::string get_config_directory();
std::string get_default_config_path(const std::string& config_name);

// Platform-aware path utilities
bool is_windows_platform();
bool is_unix_platform();
std::string normalize_path_for_platform(const std::string& path);
std::string convert_to_native_path(const std::string& path);
std::string convert_to_unix_path(const std::string& path);
bool is_absolute_path(const std::string& path);
char get_path_separator();
std::string join_paths(const std::string& base, const std::string& relative);

// Network utilities
std::string get_local_ip_address();
bool is_valid_ip_address(const std::string& ip);
bool is_valid_port(int port);

// System utilities
uint64_t get_available_memory();
uint64_t get_network_bandwidth();
void sleep_milliseconds(int ms);

// Security utilities
std::vector<uint8_t> generate_random_bytes(size_t length);
std::string get_password_from_console(const std::string& prompt);

// Version information
std::string get_version_string();
std::string get_build_info();

} // namespace common
} // namespace netcopy

