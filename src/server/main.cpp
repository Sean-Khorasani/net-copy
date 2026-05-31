#include "server/server.h"
#include "common/utils.h"
#include "logging/logger.h"
#include "file/file_manager.h" // Added for FileManager
#include "daemon/daemon.h" // Added for daemon functionality
#include <iostream>
#include <string>
#include <filesystem> // Added for std::filesystem
#include <vector> // Added for std::vector
#include <cstring> // Added for strcmp
#ifdef _WIN32
#include <windows.h>
#endif

void print_usage(const char* program_name) {
    std::cout << "NetCopy Server - Secure File Transfer" << std::endl;
    std::cout << netcopy::common::get_version_string() << std::endl;
    std::cout << netcopy::common::get_build_info() << std::endl << std::endl;
    
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program_name << " [options]" << std::endl << std::endl;
    
    std::cout << "Options:" << std::endl;
    std::cout << "  -l, --listen ADDRESS:PORT  Listen address and port (default: 0.0.0.0:1245)" << std::endl;
    std::cout << "  -a, --access PATH          Directory allowed for file access" << std::endl;
    std::cout << "  -c, --config FILE          Use specified configuration file" << std::endl;
    std::cout << "  -d, --daemon               Run as daemon (background process)" << std::endl;
    std::cout << "  --udp                      Use UDP (R-UDP) transport instead of TCP" << std::endl;
    std::cout << "  --relay ADDRESS            Listen in relay mode OR connect to relay as a server (with --token)" << std::endl;
    std::cout << "  --token TOKEN              Handshake token for relay connection" << std::endl;
#ifdef _WIN32
    std::cout << "                             Note: On Windows, use 'start /B' for background execution" << std::endl;
#endif
    std::cout << "  --auto-create              Automatically create non-existent directories (default)" << std::endl;
    std::cout << "  --no-auto-create           Disable automatic directory creation" << std::endl;
    std::cout << "  -v, --verbose              Enable verbose logging" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl << std::endl;
    
    std::cout << "Examples:" << std::endl;
#ifdef _WIN32
    std::cout << "  " << program_name << " -l 127.0.0.1:1245 -a \"D:\\\\Work\"" << std::endl;
    std::cout << "  " << program_name << " --config server.conf" << std::endl;
    std::cout << "  start /B " << program_name << " --config server.conf" << std::endl;
    std::cout << "  " << program_name << " -v" << std::endl;
#else
    std::cout << "  " << program_name << " -l 127.0.0.1:1245 -a \"/home/shared\"" << std::endl;
    std::cout << "  " << program_name << " --daemon --config server.conf" << std::endl;
    std::cout << "  " << program_name << " -v" << std::endl;
#endif
}

struct CommandLineArgs {
    std::string listen_address;
    uint16_t listen_port = netcopy::config::defaults::kDefaultTransferPort;
    std::string access_path;
    std::string config_file;
    bool daemon = false;
    bool daemon_child = false; // Flag to indicate this is already a daemon child process
    bool auto_create_directories = true;
    bool auto_create_specified = false;
    bool verbose = false;
    std::string console_level = netcopy::config::defaults::kLogLevelInfo;
    bool help = false;
    bool version = false;
    bool udp = false;
    std::string relay;
    std::string token;
};

bool parse_listen_address(const std::string& listen_arg, std::string& address, uint16_t& port) {
    if (listen_arg.empty()) {
        std::cerr << "Error: Empty listen argument" << std::endl;
        return false;
    }

    std::string port_str;
    if (listen_arg.front() == '[') {
        size_t close_bracket = listen_arg.find(']');
        if (close_bracket == std::string::npos) {
            std::cerr << "Error: Invalid bracketed IPv6 address format. Expected: [address]:port" << std::endl;
            return false;
        }
        address = listen_arg.substr(1, close_bracket - 1);
        size_t colon_pos = listen_arg.find(':', close_bracket);
        if (colon_pos == std::string::npos || colon_pos != close_bracket + 1) {
            std::cerr << "Error: Invalid port separator after bracket. Expected [address]:port" << std::endl;
            return false;
        }
        port_str = listen_arg.substr(colon_pos + 1);
    } else {
        size_t colon_pos = listen_arg.rfind(':');
        if (colon_pos == std::string::npos) {
            std::cerr << "Error: Invalid listen address format. Expected: address:port" << std::endl;
            std::cerr << "Examples: 127.0.0.1:1245, [::1]:1245, 0.0.0.0:1245" << std::endl;
            return false;
        }
        address = listen_arg.substr(0, colon_pos);
        port_str = listen_arg.substr(colon_pos + 1);
    }
    
    if (address.empty()) {
        std::cerr << "Error: Empty address in listen argument" << std::endl;
        return false;
    }
    
    if (port_str.empty()) {
        std::cerr << "Error: Empty port in listen argument" << std::endl;
        return false;
    }
    
    try {
        int port_int = std::stoi(port_str);
        if (port_int < 1 || port_int > 65535) {
            std::cerr << "Error: Port number out of range (1-65535): " << port_int << std::endl;
            return false;
        }
        port = static_cast<uint16_t>(port_int);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid port number '" << port_str << "': " << e.what() << std::endl;
        return false;
    }
}

CommandLineArgs parse_arguments(const std::vector<std::string>& arg_list) {
    CommandLineArgs args;
    
    for (size_t i = 0; i < arg_list.size(); ++i) {
        std::string arg = arg_list[i];
        
        if (arg == "-h" || arg == "--help") {
            args.help = true;
            return args;
        } else if (arg == "--version") {
            args.version = true;
            return args;
        } else if (arg == "-l" || arg == "--listen") {
            if (i + 1 < arg_list.size()) {
                std::string listen_arg = arg_list[++i];
                if (!parse_listen_address(listen_arg, args.listen_address, args.listen_port)) {
                    throw std::runtime_error("Invalid listen address format. Use ADDRESS:PORT");
                }
            } else {
                throw std::runtime_error("Missing listen address argument");
            }
        } else if (arg == "-a" || arg == "--access") {
            if (i + 1 < arg_list.size()) {
                args.access_path = arg_list[++i];
            } else {
                throw std::runtime_error("Missing access path argument");
            }
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < arg_list.size()) {
                args.config_file = arg_list[++i];
            } else {
                throw std::runtime_error("Missing configuration file argument");
            }
        } else if (arg == "-d" || arg == "--daemon") {
            args.daemon = true;
        } else if (arg == "--daemon-child") {
            args.daemon_child = true;
        } else if (arg == "--auto-create") {
            args.auto_create_directories = true;
            args.auto_create_specified = true;
        } else if (arg == "--no-auto-create") {
            args.auto_create_directories = false;
            args.auto_create_specified = true;
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
            if (i + 1 < arg_list.size()) {
                std::string next_arg = arg_list[i + 1];
                std::string next_arg_upper = next_arg;
                std::transform(next_arg_upper.begin(), next_arg_upper.end(), next_arg_upper.begin(), ::toupper);
                if (next_arg_upper == "DEBUG" || next_arg_upper == "INFO" || next_arg_upper == "WARNING" || 
                    next_arg_upper == "WARN" || next_arg_upper == "ERROR" || next_arg_upper == "CRITICAL") {
                    args.console_level = next_arg_upper;
                    i++; // Consume the log level argument
                } else {
                    args.console_level = "DEBUG";
                }
            } else {
                args.console_level = "DEBUG";
            }
        } else if (arg == "--udp") {
            args.udp = true;
        } else if (arg == "--relay") {
            if (i + 1 < arg_list.size()) {
                args.relay = arg_list[++i];
            } else {
                throw std::runtime_error("Missing relay address argument");
            }
        } else if (arg == "--token") {
            if (i + 1 < arg_list.size()) {
                args.token = arg_list[++i];
            } else {
                throw std::runtime_error("Missing token argument");
            }
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    
    return args;
}

int server_main(int argc, char* argv[]) {
    try {
        auto arg_list = netcopy::common::preprocess_arguments(argc, argv);
        auto args = parse_arguments(arg_list);
        
        if (args.version) {
            std::cout << netcopy::common::get_version_string() << std::endl;
            return 0;
        }
        
        if (args.help) {
            print_usage(argv[0]);
            return 0;
        }
        
        // Initialize server
        netcopy::server::Server server;
        std::string config_path_used;
        bool created_config_file = false;

        // Load configuration
        if (!args.config_file.empty()) {
            if (!netcopy::file::FileManager::exists(args.config_file)) {
                netcopy::config::ServerConfig::create_default_file(args.config_file);
                created_config_file = true;
            }
            server.load_config(args.config_file);
            config_path_used = args.config_file;
        } else {
            // Try to load config from executable directory first
            std::string local_config = netcopy::config::defaults::kServerConfigFileName;
            if (netcopy::file::FileManager::exists(local_config)) {
                server.load_config(local_config);
                config_path_used = local_config;
            } else {
                // Try to load default config from system location
                std::string default_config = netcopy::common::get_default_config_path(netcopy::config::defaults::kServerConfigFileName);
                if (netcopy::file::FileManager::exists(default_config)) {
                    server.load_config(default_config);
                    config_path_used = default_config;
                } else {
                    netcopy::config::ServerConfig::create_default_file(local_config);
                    created_config_file = true;
                    server.load_config(local_config);
                    config_path_used = local_config;
                }
            }
        }
        
        // Override config with command line arguments
        auto config = server.get_config();
        if (!args.listen_address.empty()) {
            config.listen_address = args.listen_address;
            config.listen_port = args.listen_port;
        }
        if (!args.access_path.empty()) {
            std::string path = args.access_path;
            // Trim leading and trailing whitespace, single/double quotes
            size_t start = path.find_first_not_of(" \t\r\n\"'");
            size_t end = path.find_last_not_of(" \t\r\n\"'");
            if (start != std::string::npos && end != std::string::npos) {
                path = path.substr(start, end - start + 1);
            } else {
                path = "";
            }
            config.allowed_paths = {path};
        }
        if (args.daemon || args.daemon_child) {
            config.run_as_daemon = true;
#ifdef _WIN32
            // On Windows GUI build, disable console output for daemon mode
            config.console.enable = false;
#endif
        }
        // Note: verbose console level settings are processed in logger reconfiguration below
        if (args.auto_create_specified) {
            config.auto_create_directories = args.auto_create_directories;
        }
        if (args.udp) {
            config.udp = true;
        }

        // Prompt for secret key if not found in config
        if (config.internal.secret_key.empty()) {
            if (args.daemon || args.daemon_child) {
                throw std::runtime_error("Secret key not found in configuration. Daemon mode requires secret_key in config file.");
            }
            config.internal.secret_key = netcopy::common::get_password_from_console("Enter secret key: ");
            if (config.internal.secret_key.empty()) {
                throw std::runtime_error("Secret key cannot be empty.");
            }
        }
        
        server.set_config(config);
        
        // Reconfigure logging after updating config (especially for daemon mode)
        auto& logger = netcopy::logging::Logger::instance();
        logger.set_level(netcopy::logging::Logger::string_to_level(config.logging.level));
        
        if (args.verbose) {
            logger.set_console_level(netcopy::logging::Logger::string_to_level(args.console_level));
            if (!config.run_as_daemon) {
                logger.set_console_output(true);
            } else {
                logger.set_console_output(false);
            }
        } else {
            logger.set_console_level(netcopy::logging::Logger::string_to_level(config.console.level));
            logger.set_console_output(config.console.enable);
        }
        
        logger.set_file_output(config.logging.enable ? config.logging.file : "");
        logger.set_json_format(config.logging.format == netcopy::config::defaults::kLogFormatJson);

        if (created_config_file) {
            LOG_INFO("Created default server configuration file: " + config_path_used);
        }
        LOG_INFO("Using server configuration from: " + config_path_used);
        
        // Start server
        if (!args.relay.empty()) {
            if (args.daemon || args.daemon_child) {
                netcopy::daemon::Daemon::setup_signal_handlers();
                if (!config.pid_file.empty()) {
                    netcopy::daemon::Daemon::create_pid_file(config.pid_file);
                }
            }
            if (args.token.empty()) {
                server.run_relay_server(args.relay);
            } else {
                server.run_relay_client(args.relay, args.token);
            }
        } else if (args.daemon || args.daemon_child) {
            server.run_as_daemon();
        } else {
            server.start();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    // Enable UTF-8 console output
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::vector<std::string> utf8_args_storage;
    std::vector<char*> utf8_argv;
    utf8_args_storage.reserve(argc);
    utf8_argv.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        utf8_args_storage.push_back(std::filesystem::path(argv[i]).u8string());
    }
    for (int i = 0; i < argc; ++i) {
        utf8_argv.push_back(const_cast<char*>(utf8_args_storage[i].c_str()));
    }
    return server_main(argc, utf8_argv.data());
}
#else
int main(int argc, char* argv[]) {
    return server_main(argc, argv);
}
#endif
