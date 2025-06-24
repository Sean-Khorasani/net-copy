#include "daemon/daemon.h"
#include "logging/logger.h"
#include "exceptions.h"

#ifdef _WIN32
#include <windows.h> // For FreeConsole, HANDLE, OpenProcess, CloseHandle, GetCurrentProcessId
#include <shellapi.h> // For ShellExecute
#include <process.h> // For _beginthreadex
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>
#endif

#include <fstream>
#include <iostream>
#include <cstdlib>

namespace netcopy {
namespace daemon {

std::string Daemon::current_pid_file_;


void Daemon::daemonize() {
#ifdef _WIN32
    // On Windows, for now just do nothing in daemon mode
    // This avoids any console redirection issues that could cause hanging
    // Users should use: start /B command for background execution
#else
    // Fork the first time
    pid_t pid = fork();
    if (pid < 0) {
        throw NetCopyException("Failed to fork process");
    }
    
    // Exit parent process
    if (pid > 0) {
        exit(0);
    }
    
    // Create new session
    if (setsid() < 0) {
        throw NetCopyException("Failed to create new session");
    }
    
    // Fork again to ensure we're not session leader
    pid = fork();
    if (pid < 0) {
        throw NetCopyException("Failed to fork second time");
    }
    
    // Exit first child
    if (pid > 0) {
        exit(0);
    }
    
    // Change working directory to root
    if (chdir("/") < 0) {
        throw NetCopyException("Failed to change working directory");
    }
    
    // Set file permissions
    umask(0);
    
    // Close all open file descriptors
    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }
    
    // Redirect stdin, stdout, stderr to /dev/null
    int fd = open("/dev/null", O_RDWR);
    if (fd != 0) {
        dup2(fd, 0);
        close(fd);
    }
    
    fd = open("/dev/null", O_RDWR);
    if (fd != 1) {
        dup2(fd, 1);
        close(fd);
    }
    
    fd = open("/dev/null", O_RDWR);
    if (fd != 2) {
        dup2(fd, 2);
        close(fd);
    }
#endif
    
    LOG_INFO("Process daemonized successfully");
}

void Daemon::create_pid_file(const std::string& pid_file) {
    current_pid_file_ = pid_file;
    
    if (is_running(pid_file)) {
        throw NetCopyException("Another instance is already running");
    }
    
    std::ofstream file(pid_file);
    if (!file) {
        throw NetCopyException("Failed to create PID file: " + pid_file);
    }
    
    file << get_pid() << std::endl;
    LOG_INFO("Created PID file: " + pid_file);
}

void Daemon::remove_pid_file(const std::string& pid_file) {
    if (std::remove(pid_file.c_str()) == 0) {
        LOG_INFO("Removed PID file: " + pid_file);
    } else {
        LOG_WARNING("Failed to remove PID file: " + pid_file);
    }
}

bool Daemon::is_running(const std::string& pid_file) {
    std::ifstream file(pid_file);
    if (!file) {
        return false; // PID file doesn't exist
    }
    
    int pid;
    file >> pid;
    file.close();
    
    if (pid <= 0) {
        return false; // Invalid PID
    }
    
#ifdef _WIN32
    // On Windows, we can use OpenProcess to check if process exists
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (process) {
        DWORD exitCode;
        if (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE) {
            CloseHandle(process);
            return true;
        }
        CloseHandle(process);
    }
    return false;
#else
    // On Unix, send signal 0 to check if process exists
    return kill(pid, 0) == 0;
#endif
}

void Daemon::setup_signal_handlers() {
#ifndef _WIN32
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe
#endif
}

int Daemon::get_pid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

void Daemon::signal_handler(int signal_num) {
    switch (signal_num) {
        case SIGINT:
            LOG_INFO("Received termination signal, shutting down gracefully");
            if (!current_pid_file_.empty()) {
                remove_pid_file(current_pid_file_);
            }
            exit(0);
            break;
#ifndef _WIN32
        case SIGHUP:
            LOG_INFO("Received SIGHUP, reloading configuration");
            // TODO: Implement configuration reload
            break;
#endif
        default:
            LOG_WARNING("Received unknown signal: " + std::to_string(signal_num));
            break;
    }
}

} // namespace daemon
} // namespace netcopy


