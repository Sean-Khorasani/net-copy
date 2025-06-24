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
#ifdef _WIN32
    std::cout << "                             Note: On Windows, use 'start /B' for background execution" << std::endl;
#endif
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
    uint16_t listen_port = 1245;
    std::string access_path;
    std::string config_file;
    bool daemon = false;
    bool daemon_child = false; // Flag to indicate this is already a daemon child process
    bool verbose = false;
    bool help = false;
};

bool parse_listen_address(const std::string& listen_arg, std::string& address, uint16_t& port) {
    size_t colon_pos = listen_arg.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "Error: Invalid listen address format. Expected: address:port" << std::endl;
        std::cerr << "Examples: 127.0.0.1:1245, 0.0.0.0:1245, 192.168.1.100:8080" << std::endl;
        return false;
    }
    
    address = listen_arg.substr(0, colon_pos);
    std::string port_str = listen_arg.substr(colon_pos + 1);
    
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

CommandLineArgs parse_arguments(int argc, char* argv[]) {
    CommandLineArgs args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.help = true;
            return args;
        } else if (arg == "-l" || arg == "--listen") {
            if (i + 1 < argc) {
                std::string listen_arg = argv[++i];
                if (!parse_listen_address(listen_arg, args.listen_address, args.listen_port)) {
                    throw std::runtime_error("Invalid listen address format. Use ADDRESS:PORT");
                }
            } else {
                throw std::runtime_error("Missing listen address argument");
            }
        } else if (arg == "-a" || arg == "--access") {
            if (i + 1 < argc) {
                args.access_path = argv[++i];
            } else {
                throw std::runtime_error("Missing access path argument");
            }
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                args.config_file = argv[++i];
            } else {
                throw std::runtime_error("Missing configuration file argument");
            }
        } else if (arg == "-d" || arg == "--daemon") {
            args.daemon = true;
        } else if (arg == "--daemon-child") {
            args.daemon_child = true;
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    
    return args;
}

int server_main(int argc, char* argv[]) {
    try {
        auto args = parse_arguments(argc, argv);
        
        if (args.help) {
            print_usage(argv[0]);
            return 0;
        }
        
        // Initialize server
        netcopy::server::Server server;
        std::string config_path_used;

        // Load configuration
        if (!args.config_file.empty()) {
            server.load_config(args.config_file);
            config_path_used = args.config_file;
        } else {
            // Try to load config from executable directory first
            std::string local_config = "server.conf";
            if (netcopy::file::FileManager::exists(local_config)) {
                server.load_config(local_config);
                config_path_used = local_config;
            } else {
                // Try to load default config from system location
                std::string default_config = netcopy::common::get_default_config_path("server.conf");
                if (netcopy::file::FileManager::exists(default_config)) {
                    server.load_config(default_config);
                    config_path_used = default_config;
                } else {
                    std::cout << "No configuration file loaded. Using default settings." << std::endl;
                    config_path_used = "(default settings)";
                }
            }
        }
        std::cout << "Using server configuration from: " << config_path_used << std::endl;
        
        // Override config with command line arguments
        auto config = server.get_config();
        if (!args.listen_address.empty()) {
            config.listen_address = args.listen_address;
            config.listen_port = args.listen_port;
        }
        if (!args.access_path.empty()) {
            config.allowed_paths = {args.access_path};
        }
        if (args.daemon || args.daemon_child) {
            config.run_as_daemon = true;
#ifdef _WIN32
            // On Windows GUI build, disable console output for daemon mode
            config.console_output = false;
#endif
        }
        if (args.verbose) {
            config.log_level = "DEBUG";
            // Note: In daemon mode, verbose still disables console output
        }

        // Prompt for secret key if not found in config
        if (config.secret_key.empty()) {
            if (args.daemon || args.daemon_child) {
                throw std::runtime_error("Secret key not found in configuration. Daemon mode requires secret_key in config file.");
            }
            config.secret_key = netcopy::common::get_password_from_console("Enter secret key: ");
            if (config.secret_key.empty()) {
                throw std::runtime_error("Secret key cannot be empty.");
            }
        }
        
        server.set_config(config);
        
        // Reconfigure logging after updating config (especially for daemon mode)
        auto& logger = netcopy::logging::Logger::instance();
        logger.set_level(netcopy::logging::Logger::string_to_level(config.log_level));
        logger.set_console_output(config.console_output);
        if (!config.log_file.empty()) {
            logger.set_file_output(config.log_file);
        }
        
        // Start server
        if (args.daemon || args.daemon_child) {
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

int main(int argc, char* argv[]) {
    return server_main(argc, argv);
}

