#pragma once

#include <string>

namespace netcopy {
namespace daemon {

class Daemon {
public:
    // Daemonize the current process
    static void daemonize();
    
    // Create and write PID file
    static void create_pid_file(const std::string& pid_file);
    
    // Remove PID file
    static void remove_pid_file(const std::string& pid_file);
    
    // Check if another instance is running
    static bool is_running(const std::string& pid_file);
    
    // Signal handling
    static void setup_signal_handlers();
    
    // Get current process ID
    static int get_pid();

private:
    static void signal_handler(int signal);
    static std::string current_pid_file_;
};

} // namespace daemon
} // namespace netcopy

