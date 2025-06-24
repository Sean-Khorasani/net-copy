#include "logging/logger.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace netcopy {
namespace logging {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Logger::set_file_output(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    log_file_ = filename;
}

void Logger::set_console_output(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    console_output_ = enable;
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (level < level_) {
        return;
    }
    
    std::string formatted_message = format_message(level, message);
    
    if (console_output_) {
        if (level >= LogLevel::LOG_ERROR) {
            std::cerr << formatted_message << std::endl;
        } else {
            std::cout << formatted_message << std::endl;
        }
    }
    
    if (!log_file_.empty()) {
        std::ofstream file(log_file_, std::ios::app);
        if (file) {
            file << formatted_message << std::endl;
        }
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::LOG_ERROR, message);
}

void Logger::critical(const std::string& message) {
    log(LogLevel::CRITICAL, message);
}

LogLevel Logger::string_to_level(const std::string& level_str) {
    std::string lower_str = level_str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    
    if (lower_str == "debug") return LogLevel::DEBUG;
    if (lower_str == "info") return LogLevel::INFO;
    if (lower_str == "warning" || lower_str == "warn") return LogLevel::WARNING;
    if (lower_str == "error") return LogLevel::LOG_ERROR;
    if (lower_str == "critical") return LogLevel::CRITICAL;
    
    return LogLevel::INFO; // Default
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::LOG_ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string Logger::format_message(LogLevel level, const std::string& message) {
    std::ostringstream oss;
    oss << "[" << get_timestamp() << "] "
        << "[" << level_to_string(level) << "] "
        << message;
    return oss.str();
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

} // namespace logging
} // namespace netcopy

