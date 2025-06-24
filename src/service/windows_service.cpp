#ifdef _WIN32
#include "service/windows_service.h"
#include <iostream>
#include <filesystem>
#include <sstream>

namespace netcopy {
namespace service {

// Static members
SERVICE_STATUS WindowsService::service_status_ = {};
SERVICE_STATUS_HANDLE WindowsService::service_status_handle_ = nullptr;
WindowsService* WindowsService::instance_ = nullptr;

WindowsService::WindowsService(const std::string& service_name, const std::string& display_name)
    : service_name_(service_name)
    , display_name_(display_name)
    , server_process_handle_(INVALID_HANDLE_VALUE)
    , server_process_id_(0) {
    instance_ = this;
    
    // Set default server executable path
    server_executable_ = get_executable_directory() + "\\net_copy_server.exe";
}

WindowsService::~WindowsService() {
    if (server_process_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(server_process_handle_);
    }
}

bool WindowsService::install_service(const std::string& executable_path) {
    SC_HANDLE sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!sc_manager) {
        log_error("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = CreateServiceA(
        sc_manager,
        service_name_.c_str(),
        display_name_.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        executable_path.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    bool success = (service != nullptr);
    if (!success) {
        DWORD error = GetLastError();
        if (error == ERROR_SERVICE_EXISTS) {
            std::cout << "Service already exists." << std::endl;
            success = true;
        } else {
            log_error("Failed to create service");
        }
    } else {
        std::cout << "Service installed successfully." << std::endl;
    }

    if (service) CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return success;
}

bool WindowsService::uninstall_service() {
    SC_HANDLE sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!sc_manager) {
        log_error("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = OpenServiceA(sc_manager, service_name_.c_str(), SERVICE_STOP | DELETE);
    if (!service) {
        log_error("Failed to open service");
        CloseServiceHandle(sc_manager);
        return false;
    }

    // Stop service if running
    SERVICE_STATUS status;
    if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
        std::cout << "Stopping service..." << std::endl;
        Sleep(1000);
    }

    bool success = DeleteService(service) != 0;
    if (success) {
        std::cout << "Service uninstalled successfully." << std::endl;
    } else {
        log_error("Failed to delete service");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return success;
}

bool WindowsService::start_service() {
    SC_HANDLE sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!sc_manager) {
        log_error("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = OpenServiceA(sc_manager, service_name_.c_str(), SERVICE_START | SERVICE_QUERY_STATUS);
    if (!service) {
        log_error("Failed to open service");
        CloseServiceHandle(sc_manager);
        return false;
    }

    bool success = StartServiceA(service, 0, nullptr) != 0;
    if (success) {
        std::cout << "Service start command sent successfully." << std::endl;
        
        // Wait a moment and check if service is actually running
        Sleep(2000);
        SERVICE_STATUS status;
        if (QueryServiceStatus(service, &status)) {
            if (status.dwCurrentState == SERVICE_RUNNING) {
                std::cout << "Service is now running." << std::endl;
            } else if (status.dwCurrentState == SERVICE_STOPPED) {
                std::cout << "WARNING: Service started but then stopped immediately." << std::endl;
                std::cout << "Exit code: " << status.dwWin32ExitCode << std::endl;
                success = false;
            } else {
                std::cout << "Service state: " << status.dwCurrentState << std::endl;
            }
        }
    } else {
        DWORD error = GetLastError();
        if (error == ERROR_SERVICE_ALREADY_RUNNING) {
            std::cout << "Service is already running." << std::endl;
            success = true;
        } else {
            log_error("Failed to start service");
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return success;
}

bool WindowsService::stop_service() {
    SC_HANDLE sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!sc_manager) {
        log_error("Failed to open Service Control Manager");
        return false;
    }

    SC_HANDLE service = OpenServiceA(sc_manager, service_name_.c_str(), SERVICE_STOP);
    if (!service) {
        log_error("Failed to open service");
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    bool success = ControlService(service, SERVICE_CONTROL_STOP, &status) != 0;
    if (success) {
        std::cout << "Service stopped successfully." << std::endl;
    } else {
        log_error("Failed to stop service");
    }

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return success;
}

bool WindowsService::is_service_running() {
    SC_HANDLE sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!sc_manager) return false;

    SC_HANDLE service = OpenServiceA(sc_manager, service_name_.c_str(), SERVICE_QUERY_STATUS);
    if (!service) {
        CloseServiceHandle(sc_manager);
        return false;
    }

    SERVICE_STATUS status;
    bool success = QueryServiceStatus(service, &status) != 0;
    bool running = success && (status.dwCurrentState == SERVICE_RUNNING);

    CloseServiceHandle(service);
    CloseServiceHandle(sc_manager);
    return running;
}

void WindowsService::run_service() {
    SERVICE_TABLE_ENTRYA service_table[] = {
        { const_cast<char*>(service_name_.c_str()), service_main },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherA(service_table)) {
        log_error("StartServiceCtrlDispatcher failed");
    }
}

void WINAPI WindowsService::service_main(DWORD argc, LPTSTR* argv) {
    if (!instance_) {
        OutputDebugStringA("NetCopy Service: instance_ is null");
        return;
    }

    OutputDebugStringA("NetCopy Service: Registering control handler");
    service_status_handle_ = RegisterServiceCtrlHandlerA(
        instance_->service_name_.c_str(),
        service_ctrl_handler
    );

    if (!service_status_handle_) {
        OutputDebugStringA("NetCopy Service: RegisterServiceCtrlHandler failed");
        instance_->log_error("RegisterServiceCtrlHandler failed");
        return;
    }

    OutputDebugStringA("NetCopy Service: Initializing service status");
    // Initialize service status
    service_status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    service_status_.dwCurrentState = SERVICE_START_PENDING;
    service_status_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    service_status_.dwWin32ExitCode = 0;
    service_status_.dwServiceSpecificExitCode = 0;
    service_status_.dwCheckPoint = 0;
    service_status_.dwWaitHint = 0;

    OutputDebugStringA("NetCopy Service: Reporting start pending");
    instance_->report_service_status(SERVICE_START_PENDING, NO_ERROR, 3000);

    // Start the server process
    if (instance_->start_server_process()) {
        instance_->report_service_status(SERVICE_RUNNING, NO_ERROR, 0);

        // Wait for the server process to exit
        if (instance_->server_process_handle_ != INVALID_HANDLE_VALUE) {
            DWORD wait_result = WaitForSingleObject(instance_->server_process_handle_, INFINITE);
            
            // Check exit code
            DWORD exit_code = 0;
            if (GetExitCodeProcess(instance_->server_process_handle_, &exit_code)) {
                if (exit_code != 0) {
                    std::string error_msg = "NetCopy server failed to start (exit code: " + std::to_string(exit_code) + 
                                          "). Check server.conf file and ensure secret_key is configured.";
                    instance_->log_error(error_msg);
                    instance_->report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
                    return;
                }
            }
        }
    } else {
        instance_->report_service_status(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
        return;
    }

    instance_->report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
}

void WINAPI WindowsService::service_ctrl_handler(DWORD ctrl_code) {
    if (!instance_) return;

    switch (ctrl_code) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            instance_->report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
            instance_->stop_server_process();
            instance_->report_service_status(SERVICE_STOPPED, NO_ERROR, 0);
            break;

        case SERVICE_CONTROL_INTERROGATE:
            break;

        default:
            break;
    }
}

bool WindowsService::start_server_process() {
    // Build command line for server with config file
    std::string config_path = get_executable_directory() + "\\server.conf";
    std::string cmd_line = "\"" + server_executable_ + "\" --daemon --config \"" + config_path + "\" --verbose";

    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    // Get working directory (same as executable directory)
    std::string working_dir = get_executable_directory();

    // Start the server process
    BOOL success = CreateProcessA(
        nullptr,
        const_cast<char*>(cmd_line.c_str()),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        working_dir.c_str(),
        &si,
        &pi
    );

    if (success) {
        server_process_handle_ = pi.hProcess;
        server_process_id_ = pi.dwProcessId;
        CloseHandle(pi.hThread);
        return true;
    } else {
        log_error("Failed to start server process: " + server_executable_);
        return false;
    }
}

bool WindowsService::stop_server_process() {
    if (server_process_handle_ == INVALID_HANDLE_VALUE) {
        return true;
    }

    // Try to terminate gracefully first
    if (GenerateConsoleCtrlEvent(CTRL_C_EVENT, server_process_id_)) {
        // Wait for graceful shutdown
        if (WaitForSingleObject(server_process_handle_, 5000) == WAIT_OBJECT_0) {
            CloseHandle(server_process_handle_);
            server_process_handle_ = INVALID_HANDLE_VALUE;
            return true;
        }
    }

    // Force termination if graceful shutdown failed
    BOOL success = TerminateProcess(server_process_handle_, 1);
    CloseHandle(server_process_handle_);
    server_process_handle_ = INVALID_HANDLE_VALUE;
    
    return success != 0;
}

void WindowsService::report_service_status(DWORD current_state, DWORD exit_code, DWORD wait_hint) {
    static DWORD check_point = 1;

    service_status_.dwCurrentState = current_state;
    service_status_.dwWin32ExitCode = exit_code;
    service_status_.dwWaitHint = wait_hint;

    if (current_state == SERVICE_START_PENDING) {
        service_status_.dwControlsAccepted = 0;
    } else {
        service_status_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if ((current_state == SERVICE_RUNNING) || (current_state == SERVICE_STOPPED)) {
        service_status_.dwCheckPoint = 0;
    } else {
        service_status_.dwCheckPoint = check_point++;
    }

    SetServiceStatus(service_status_handle_, &service_status_);
}

void WindowsService::log_error(const std::string& message) {
    // Log to console/stderr only
    // Windows Event Log requires message resource DLL for proper formatting
    std::cerr << "NetCopy Service Error: " << message << std::endl;
}

std::string WindowsService::get_executable_directory() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::filesystem::path exe_path(path);
    return exe_path.parent_path().string();
}

} // namespace service
} // namespace netcopy

#endif // _WIN32