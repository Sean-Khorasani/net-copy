#pragma once

#ifdef _WIN32
#include <windows.h>
#include <winsvc.h>
#include <string>

namespace netcopy {
namespace service {

class WindowsService {
public:
    WindowsService(const std::string& service_name, const std::string& display_name);
    ~WindowsService();

    // Service management
    bool install_service(const std::string& executable_path);
    bool uninstall_service();
    bool start_service();
    bool stop_service();
    bool is_service_running();

    // Service control
    void run_service();
    static void WINAPI service_main(DWORD argc, LPTSTR* argv);
    static void WINAPI service_ctrl_handler(DWORD ctrl_code);

    // Process management
    bool start_server_process();
    bool stop_server_process();

private:
    std::string service_name_;
    std::string display_name_;
    std::string server_executable_;
    
    // Service status
    static SERVICE_STATUS service_status_;
    static SERVICE_STATUS_HANDLE service_status_handle_;
    static WindowsService* instance_;
    
    // Process handles
    HANDLE server_process_handle_;
    DWORD server_process_id_;
    
    // Helper methods
    void report_service_status(DWORD current_state, DWORD exit_code, DWORD wait_hint);
    void log_error(const std::string& message);
    std::string get_executable_directory();
};

} // namespace service
} // namespace netcopy

#endif // _WIN32