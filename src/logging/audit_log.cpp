#include "logging/audit_log.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace netcopy {
namespace logging {

AuditLog& AuditLog::instance() {
    static AuditLog instance;
    return instance;
}

AuditLog::~AuditLog() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) {
        stream_.close();
    }
}

void AuditLog::set_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    if (stream_.is_open()) {
        stream_.close();
    }
    stream_.open(path_, std::ios::app);
}

void AuditLog::log_connect(const std::string& user, const std::string& client_address, bool success) {
    std::string u = user.empty() ? "anonymous" : user;
    std::string status = success ? "SUCCESS" : "FAILED";
    std::ostringstream oss;
    oss << get_timestamp() << "  CONNECT  " << u << "  " << client_address << "  " << status;
    write_entry(oss.str());
}

void AuditLog::log_disconnect(const std::string& user, const std::string& client_address) {
    std::string u = user.empty() ? "anonymous" : user;
    std::ostringstream oss;
    oss << get_timestamp() << "  DISCONNECT  " << u << "  " << client_address;
    write_entry(oss.str());
}

void AuditLog::log_transfer(const std::string& user,
                            const std::string& client_address,
                            const std::string& file_path,
                            uint64_t bytes,
                            double duration,
                            const std::string& checksum,
                            bool success) {
    std::string u = user.empty() ? "anonymous" : user;
    std::string status = success ? "SUCCESS" : "FAILED";
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::ostringstream oss;
    oss << get_timestamp() << "  TRANSFER  " << u << "  " << client_address
        << "  " << file_path << "  (" << std::fixed << std::setprecision(1) << mb << " MB, "
        << std::setprecision(3) << duration << "s, sha3=" << (checksum.empty() ? "N/A" : checksum)
        << ")  " << status;
    write_entry(oss.str());
}

void AuditLog::write_entry(const std::string& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_.is_open()) {
        stream_ << entry << "\n";
        stream_.flush();
    }
}

std::string AuditLog::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    #ifdef _WIN32
    struct tm buf;
    if (gmtime_s(&buf, &time_t) == 0) {
        oss << std::put_time(&buf, "%Y-%m-%dT%H:%M:%SZ");
    }
    #else
    struct tm buf;
    if (gmtime_r(&time_t, &buf) != nullptr) {
        oss << std::put_time(&buf, "%Y-%m-%dT%H:%M:%SZ");
    }
    #endif
    return oss.str();
}

} // namespace logging
} // namespace netcopy
