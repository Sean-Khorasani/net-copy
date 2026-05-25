#include "gui/gui_server.h"
#include "common/utils.h"
#include "logging/logger.h"
#include "common/fast_mem.h"
#include <iostream>
#include <string>
#include <atomic>
#include <chrono>
#include <thread>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
std::atomic<bool> g_running(true);

#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}
#else
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = false;
    }
}
#endif
} // namespace

int main(int argc, char* argv[]) {
    // 1. Handle command line options
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--version" || arg == "-v") {
            std::cout << netcopy::common::get_version_string() << std::endl;
            std::cout << netcopy::common::get_build_info() << std::endl;
            return 0;
        } else if (arg == "--help" || arg == "-h" || arg == "/?") {
            std::cout << "NetCopy GUI Client" << std::endl;
            std::cout << netcopy::common::get_version_string() << std::endl << std::endl;
            std::cout << "Usage:" << std::endl;
            std::cout << "  net_copy_gui.exe [options]" << std::endl << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  -v, --version  Show version information" << std::endl;
            std::cout << "  -h, --help     Show this help message" << std::endl;
            return 0;
        }
    }

    // Disable console output for the GUI server, everything goes to log file
    netcopy::logging::Logger::instance().set_file_output("net_copy_gui.log");
    netcopy::logging::Logger::instance().set_level(netcopy::logging::LogLevel::INFO);
    netcopy::logging::Logger::instance().set_console_output(false);

    // 2. Set up OS shutdown handlers
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(console_ctrl_handler, TRUE)) {
        LOG_ERROR("Warning: Could not set console control handler.");
    }
#else
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#endif

    LOG_INFO("Starting NetCopy GUI Client...");

    // 3. Initialize and start GuiServer
    netcopy::gui::GuiServer server;
    uint16_t start_port = 1246;
    if (!server.start(start_port)) {
        LOG_ERROR("Error: Could not start GUI server on port " + std::to_string(start_port) + " or fallbacks.");
        return 1;
    }

    uint16_t port = server.get_port();
    std::string url = "http://localhost:" + std::to_string(port) + "/";
    LOG_INFO("GUI Server running at: " + url);

    // 4. Open browser
#ifdef _WIN32
    LOG_INFO("Opening system browser...");
    HINSTANCE result = ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        LOG_ERROR("Warning: Failed to open system browser automatically (Error code " + std::to_string(reinterpret_cast<INT_PTR>(result)) + ").");
        LOG_INFO("Please open the URL manually in your browser.");
    }
#else
    LOG_INFO("Please open the following link in your browser: " + url);
#endif

    LOG_INFO("Press Ctrl+C to exit and stop the server.");

    // 5. Keep running
    while (g_running && server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    LOG_INFO("Stopping NetCopy GUI Client...");
    server.stop();
    LOG_INFO("GUI Client stopped successfully.");

    return 0;
}
