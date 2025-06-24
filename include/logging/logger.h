#pragma once

#include <string>
#include <memory>
#include <mutex>

namespace netcopy {
namespace logging {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    LOG_ERROR = 3,
    CRITICAL = 4
};

class Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level);
    void set_file_output(const std::string& filename);
    void set_console_output(bool enable);
    
    void log(LogLevel level, const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
    static LogLevel string_to_level(const std::string& level_str);
    static std::string level_to_string(LogLevel level);

private:
    Logger() = default;
    ~Logger() = default;
    
    LogLevel level_ = LogLevel::INFO;
    std::string log_file_;
    bool console_output_ = true;
    std::mutex mutex_;
    
    std::string format_message(LogLevel level, const std::string& message);
    std::string get_timestamp();
};

// Convenience macros
#define LOG_DEBUG(msg) netcopy::logging::Logger::instance().debug(msg)
#define LOG_INFO(msg) netcopy::logging::Logger::instance().info(msg)
#define LOG_WARNING(msg) netcopy::logging::Logger::instance().warning(msg)
#define LOG_ERROR(msg) netcopy::logging::Logger::instance().error(msg)
#define LOG_CRITICAL(msg) netcopy::logging::Logger::instance().critical(msg)

} // namespace logging
} // namespace netcopy

