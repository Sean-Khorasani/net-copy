#pragma once

#include <string>
#include <mutex>
#include <fstream>

namespace netcopy {
namespace logging {

class AuditLog {
public:
    static AuditLog& instance();

    void set_path(const std::string& path);
    void log_connect(const std::string& user, const std::string& client_address, bool success);
    void log_disconnect(const std::string& user, const std::string& client_address);
    void log_transfer(const std::string& user,
                      const std::string& client_address,
                      const std::string& file_path,
                      uint64_t bytes,
                      double duration,
                      const std::string& checksum,
                      bool success);

private:
    AuditLog() = default;
    ~AuditLog();

    std::string path_;
    std::mutex mutex_;
    std::ofstream stream_;

    void write_entry(const std::string& entry);
    std::string get_timestamp();
};

} // namespace logging
} // namespace netcopy
