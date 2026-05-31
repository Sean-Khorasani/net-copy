#include "gui/gui_server.h"
#include "common/utils.h"
#include "logging/logger.h"
#include "common/fast_mem.h"
#include "config/config_parser.h"
#include "file/file_manager.h"
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
    std::string config_file_arg;

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
            std::cout << "  -c, --config   Use specified client configuration file" << std::endl;
            return 0;
        } else if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) {
                std::cerr << "Error: Missing configuration file argument" << std::endl;
                return 1;
            }
            config_file_arg = argv[++i];
        } else {
            std::cerr << "Error: Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    // Load ClientConfig to get GUI settings
    netcopy::config::ClientConfig config = netcopy::config::ClientConfig::get_default();
    std::string config_file;
    bool created_config_file = false;
    try {
        if (!config_file_arg.empty()) {
            config_file = config_file_arg;
            if (!netcopy::file::FileManager::exists(config_file)) {
                netcopy::config::ClientConfig::create_default_file(config_file);
                created_config_file = true;
            }
        } else {
            config_file = netcopy::config::defaults::kClientConfigFileName;
            if (!netcopy::file::FileManager::exists(config_file)) {
                std::string default_config = netcopy::common::get_default_config_path(netcopy::config::defaults::kClientConfigFileName);
                if (netcopy::file::FileManager::exists(default_config)) {
                    config_file = default_config;
                } else {
                    netcopy::config::ClientConfig::create_default_file(config_file);
                    created_config_file = true;
                }
            }
        }
        config = netcopy::config::ClientConfig::load_from_file(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to load client configuration: " << e.what() << std::endl;
        return 1;
    }

    auto& logger = netcopy::logging::Logger::instance();
    logger.set_level(netcopy::logging::Logger::string_to_level(config.logging.level));
    logger.set_console_level(netcopy::logging::Logger::string_to_level(config.console.level));
    logger.set_console_output(config.console.enable);
    logger.set_json_format(config.logging.format == netcopy::config::defaults::kLogFormatJson);
    logger.set_file_output(config.logging.enable ? config.logging.file : "");
    if (created_config_file) {
        LOG_INFO("Created default client configuration file: " + config_file);
    }
    LOG_INFO("Loaded GUI configuration from " + config_file);

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
    uint16_t start_port = config.gui.port;
    if (!server.start(start_port)) {
        LOG_ERROR("Error: Could not start GUI server on port " + std::to_string(start_port) + " or fallbacks.");
        return 1;
    }

    uint16_t port = server.get_port();
    std::string url = "http://localhost:" + std::to_string(port) + "/";
    
    if (config.gui.theme != "system" || config.gui.language != "en") {
        url += "?theme=" + config.gui.theme + "&lang=" + config.gui.language;
    }
    
    LOG_INFO("GUI Server running at: " + url);

    // 4. Open browser
    if (config.gui.open_browser_on_start) {
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
    } else {
        LOG_INFO("Browser auto-open disabled. Please open the following link in your browser: " + url);
    }

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
