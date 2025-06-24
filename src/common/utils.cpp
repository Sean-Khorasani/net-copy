#include "common/utils.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h> // Added for inet_pton
#include <iphlpapi.h>
#include <conio.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>
#include <sys/sysinfo.h>
#endif

namespace netcopy {
namespace common {

std::string to_hex_string(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> from_hex_string(const std::string& hex_str) {
    std::vector<uint8_t> data;
    for (size_t i = 0; i < hex_str.length(); i += 2) {
        std::string byte_str = hex_str.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
        data.push_back(byte);
    }
    return data;
}

std::string get_executable_path() {
#ifdef _WIN32
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return std::string(path);
#else
    char path[1024];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::string(path);
    }
    return "";
#endif
}

std::string get_config_directory() {
#ifdef _WIN32
    char* appdata = getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "\\NetCopy";
    }
    return ".\\config";
#else
    char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/netcopy";
    }
    return "./config";
#endif
}

std::string get_default_config_path(const std::string& config_name) {
    std::filesystem::path config_dir = get_config_directory();
    return (config_dir / config_name).string();
}

std::string get_local_ip_address() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    
    struct hostent* host_entry = gethostbyname(hostname);
    if (host_entry) {
        return std::string(inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0])));
    }
    
    WSACleanup();
    return "127.0.0.1";
#else
    struct ifaddrs* ifaddrs_ptr;
    if (getifaddrs(&ifaddrs_ptr) == -1) {
        return "127.0.0.1";
    }
    
    for (struct ifaddrs* ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* addr_in = (struct sockaddr_in*)ifa->ifa_addr;
            std::string ip = inet_ntoa(addr_in->sin_addr);
            if (ip != "127.0.0.1") {
                freeifaddrs(ifaddrs_ptr);
                return ip;
            }
        }
    }
    
    freeifaddrs(ifaddrs_ptr);
    return "127.0.0.1";
#endif
}

bool is_valid_ip_address(const std::string& ip) {
#ifdef _WIN32
    // Use inet_addr on Windows (universally available)
    return inet_addr(ip.c_str()) != INADDR_NONE;
#else
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
#endif
}

bool is_valid_port(int port) {
    return port > 0 && port <= 65535;
}

uint64_t get_available_memory() {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullAvailPhys;
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.freeram * info.mem_unit;
    }
    return 0;
#endif
}

uint64_t get_network_bandwidth() {
    // This is a simplified implementation
    // In a real application, you would measure actual network performance
    return 100 * 1024 * 1024; // Assume 100 Mbps
}

void sleep_milliseconds(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::vector<uint8_t> generate_random_bytes(size_t length) {
    std::vector<uint8_t> bytes(length);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (auto& byte : bytes) {
        byte = dis(gen);
    }
    
    return bytes;
}

std::string get_password_from_console(const std::string& prompt) {
    std::cout << prompt;
    std::string password;
    
#ifdef _WIN32
    char ch;
    while ((ch = _getch()) != '\r') {
        if (ch == '\b') {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b";
            }
        } else {
            password += ch;
            std::cout << '*';
        }
    }
#else
    struct termios old_termios, new_termios;
    tcgetattr(STDIN_FILENO, &old_termios);
    new_termios = old_termios;
    new_termios.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    char ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        if (ch == '\b' || ch == 127) {
            if (!password.empty()) {
                password.pop_back();
                std::cout << "\b \b";
            }
        } else {
            password += ch;
            std::cout << '*';
        }
    }
    
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
#endif
    
    std::cout << std::endl;
    return password;
}

std::string get_version_string() {
    return "NetCopy v1.0.0";
}

std::string get_build_info() {
    return "Built on " __DATE__ " " __TIME__;
}

// Platform-aware path utilities
bool is_windows_platform() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool is_unix_platform() {
    return !is_windows_platform();
}

std::string normalize_path_for_platform(const std::string& path) {
    return convert_to_native_path(path);
}

std::string convert_to_native_path(const std::string& path) {
    std::string result = path;
#ifdef _WIN32
    // Convert forward slashes to backslashes on Windows
    std::replace(result.begin(), result.end(), '/', '\\');
#else
    // Convert backslashes to forward slashes on Unix-like systems
    std::replace(result.begin(), result.end(), '\\', '/');
#endif
    return result;
}

std::string convert_to_unix_path(const std::string& path) {
    std::string result = path;
    // Always convert to Unix-style forward slashes
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

bool is_absolute_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    
#ifdef _WIN32
    // Windows: Check for drive letter (C:) or UNC path (\\)
    if (path.length() >= 2) {
        if ((path[1] == ':' && std::isalpha(path[0])) ||  // C:
            (path[0] == '\\' && path[1] == '\\')) {       // \\server
            return true;
        }
    }
    // Also check for Unix-style absolute paths (for compatibility)
    return path[0] == '/' || path[0] == '\\';
#else
    // Unix: Check for leading slash
    return path[0] == '/';
#endif
}

char get_path_separator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

std::string join_paths(const std::string& base, const std::string& relative) {
    if (base.empty()) {
        return relative;
    }
    if (relative.empty()) {
        return base;
    }
    
    // Check if relative path is actually absolute
    if (is_absolute_path(relative)) {
        return relative;
    }
    
    std::string result = base;
    char sep = get_path_separator();
    
    // Ensure base ends with separator
    if (result.back() != '/' && result.back() != '\\') {
        result += sep;
    }
    
    // Remove leading separators from relative path
    std::string clean_relative = relative;
    while (!clean_relative.empty() && 
           (clean_relative[0] == '/' || clean_relative[0] == '\\')) {
        clean_relative = clean_relative.substr(1);
    }
    
    result += clean_relative;
    return convert_to_native_path(result);
}

} // namespace common
} // namespace netcopy

