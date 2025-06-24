#include "client/client.h"
#include "common/utils.h"
#include "logging/logger.h"
#include "file/file_manager.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/aes_ctr.h"
#include "crypto/aes_256_gcm_gpu.h"
#include "common/bandwidth_monitor.h"
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <stdexcept>

struct CommandLineArgs {
    std::string config_file;
    std::string source_path;
    std::string destination_path;
    bool recursive = false;
    bool resume = false;
    bool verbose = false;
    bool help = false;
    uint16_t server_port = 0; // Added for client port
    bool empty_dirs_specified = false; // Track if user specified --no-empty-dirs
    bool create_empty_directories = true; // Default value
    netcopy::crypto::SecurityLevel security_level = netcopy::crypto::SecurityLevel::HIGH;
};

void print_usage(const char* program_name) {
    std::cout << "NetCopy Client - Secure File Transfer" << std::endl;
    std::cout << netcopy::common::get_version_string() << std::endl;
    std::cout << netcopy::common::get_build_info() << std::endl << std::endl;
    
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program_name << " [options] <source> <destination>" << std::endl << std::endl;
    
    std::cout << "Options:" << std::endl;
    std::cout << "  -c, --config FILE          Use specified configuration file" << std::endl;
    std::cout << "  -p, --port PORT            Specify server port number" << std::endl;
    std::cout << "  -R, --recursive            Transfer directories recursively" << std::endl;
    std::cout << "  --resume                   Resume interrupted transfer" << std::endl;
    std::cout << "  --no-empty-dirs            Don't create empty directories" << std::endl;
    std::cout << "  -s, --security LEVEL       Security level: high (default), fast, aes, or AES-256-GCM" << std::endl;
    std::cout << "  -v, --verbose              Enable verbose logging" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl << std::endl;
    
    std::cout << "Destination formats:" << std::endl;
    std::cout << "  server:port/path           e.g., 127.0.0.1:1245/D:/Work/" << std::endl;
    std::cout << "  server:/path               e.g., 127.0.0.1:/D:/Work/ (uses default/config port)" << std::endl;
    std::cout << "  server:D:\\path             e.g., 127.0.0.1:D:\\Work\\ (Windows path)" << std::endl;
    std::cout << "  server                     e.g., 127.0.0.1 (uses default port and path)" << std::endl << std::endl;
    
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " file.txt 127.0.0.1:1245/D:/Work/" << std::endl;
    std::cout << "  " << program_name << " file.txt 127.0.0.1:D:\\Work\\" << std::endl;
    std::cout << "  " << program_name << " -p 1245 file.txt 127.0.0.1:/D:/Work/" << std::endl;
    std::cout << "  " << program_name << " -R folder/ 127.0.0.1" << std::endl;
    std::cout << "  " << program_name << " ./folder/ 192.168.1.100:/remote/path/ -R" << std::endl;
    std::cout << "  " << program_name << " large_file.zip 192.168.1.100:/downloads/ --resume" << std::endl;
}

CommandLineArgs parse_arguments(int argc, char* argv[]) {
    CommandLineArgs args;
    
    std::vector<std::string> positional_args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.help = true;
            return args;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                args.config_file = argv[++i];
            } else {
                throw std::runtime_error("Missing configuration file argument");
            }
        } else if (arg == "-p" || arg == "--port") { // Added port parsing
            if (i + 1 < argc) {
                try {
                    int port_int = std::stoi(argv[++i]);
                    if (port_int < 1 || port_int > 65535) {
                        throw std::runtime_error("Port number out of range (1-65535)");
                    }
                    args.server_port = static_cast<uint16_t>(port_int);
                } catch (const std::exception& e) {
                    throw std::runtime_error("Error parsing port argument '" + std::string(argv[i]) + "': " + e.what());
                }
            } else {
                throw std::runtime_error("Missing port number argument");
            }
        } else if (arg == "-R" || arg == "--recursive") {
            args.recursive = true;
        } else if (arg == "--resume") {
            args.resume = true;
        } else if (arg == "--no-empty-dirs") {
            args.empty_dirs_specified = true;
            args.create_empty_directories = false;
        } else if (arg == "-s" || arg == "--security") {
            if (i + 1 < argc) {
                std::string level = argv[++i];
                if (level == "high") {
                    args.security_level = netcopy::crypto::SecurityLevel::HIGH;
                } else if (level == "fast") {
                    args.security_level = netcopy::crypto::SecurityLevel::FAST;
                } else if (level == "aes") {
                    args.security_level = netcopy::crypto::SecurityLevel::AES;
                } else if (level == "AES-256-GCM") {
                    args.security_level = netcopy::crypto::SecurityLevel::AES_256_GCM;
                } else {
                    throw std::runtime_error("Invalid security level '" + level + "'. Use 'high', 'fast', 'aes', or 'AES-256-GCM'.");
                }
            } else {
                throw std::runtime_error("Missing security level argument");
            }
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else {
            positional_args.push_back(arg);
        }
    }
    
    if (positional_args.size() == 2) {
        args.source_path = positional_args[0];
        args.destination_path = positional_args[1];
    } else if (!args.help) {
        if (positional_args.empty()) {
            throw std::runtime_error("Missing source and destination arguments. Use -h for help.");
        } else if (positional_args.size() == 1) {
            throw std::runtime_error("Missing destination argument. Use -h for help.");
        } else {
            throw std::runtime_error("Too many arguments. Expected: <source> <destination>. Use -h for help.");
        }
    }
    
    return args;
}

int main(int argc, char* argv[]) {
    try {
        auto args = parse_arguments(argc, argv);
        
        if (args.help) {
            print_usage(argv[0]);
            return 0;
        }
        
        netcopy::client::Client client;
        
        // Load configuration
        std::string config_path_used;
        if (!args.config_file.empty()) {
            client.load_config(args.config_file);
            config_path_used = args.config_file;
        } else {
            // Try to load config from executable directory first
            std::string local_config = "client.conf";
            if (netcopy::file::FileManager::exists(local_config)) {
                client.load_config(local_config);
                config_path_used = local_config;
            } else {
                // Try to load default config from system location
                std::string default_config = netcopy::common::get_default_config_path("client.conf");
                if (netcopy::file::FileManager::exists(default_config)) {
                    client.load_config(default_config);
                    config_path_used = default_config;
                } else {
                    config_path_used = "(default settings)";
                }
            }
        }
        
        // Override config with command line arguments
        auto config = client.get_config(); 
        if (args.verbose) {
            config.log_level = "DEBUG";
            config.console_output = true;
        } else {
            // In non-verbose mode, minimize logging output
            config.log_level = "ERROR"; // Only show errors
            config.console_output = false; // Disable debug/info logs to console
        }
        
        // Override config with command line arguments only if specified
        if (args.empty_dirs_specified) {
            config.create_empty_directories = args.create_empty_directories;
            if (args.verbose) {
                std::cout << "Command line override: create_empty_directories = " << 
                            (config.create_empty_directories ? "true" : "false") << std::endl;
            }
        } else {
            if (args.verbose) {
                std::cout << "Config setting: create_empty_directories = " << 
                            (config.create_empty_directories ? "true" : "false") << std::endl;
            }
        }
        
        // Prompt for secret key if not found in config
        if (config.secret_key.empty()) {
            std::string password = netcopy::common::get_password_from_console("Enter master password: ");
            if (password.empty()) {
                throw std::runtime_error("Password cannot be empty.");
            }
            
            // Derive key from password using a fixed salt for consistency
            // This ensures the same password always generates the same key
            std::vector<uint8_t> fixed_salt = {
                0x4e, 0x65, 0x74, 0x43, 0x6f, 0x70, 0x79, 0x53,
                0x61, 0x6c, 0x74, 0x31, 0x32, 0x33, 0x34, 0x35,
                0x36, 0x37, 0x38, 0x39, 0x30, 0x41, 0x42, 0x43,
                0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b
            };
            
            auto key = netcopy::crypto::ChaCha20Poly1305::derive_key(password, fixed_salt);
            
            // Convert to hex string format
            config.secret_key = "0x" + netcopy::common::to_hex_string(
                std::vector<uint8_t>(key.begin(), key.end()));
            
            if (args.verbose) {
                std::cout << "Generated secret key from password." << std::endl;
            }
        }

        client.set_config(config);
        
        // Reconfigure logging with the updated settings
        auto& logger = netcopy::logging::Logger::instance();
        logger.set_level(netcopy::logging::Logger::string_to_level(config.log_level));
        logger.set_console_output(config.console_output);
        if (!config.log_file.empty()) {
            logger.set_file_output(config.log_file);
        }
        
        // Log configuration loaded message only in verbose mode (after logger is reconfigured)
        if (args.verbose) {
            std::cout << "Client configuration loaded from: " << config_path_used << std::endl;
        }

        // Parse destination - support multiple formats:
        // 1. server_address:port/path  (e.g., 127.0.0.1:1245/path)
        // 2. server_address:path       (e.g., 127.0.0.1:/path, uses port from config/command line)
        // 3. server_address            (e.g., 127.0.0.1, uses port from config/command line and default path)
        
        std::string server_address;
        uint16_t server_port = args.server_port; // Use command line port if provided
        std::string remote_path = "/"; // Default path
        
        size_t colon_pos = args.destination_path.find(":");
        if (colon_pos == std::string::npos) {
            // Format: server_address (no port, no path)
            server_address = args.destination_path;
        } else {
            server_address = args.destination_path.substr(0, colon_pos);
            std::string after_colon = args.destination_path.substr(colon_pos + 1);
            
            if (after_colon.empty()) {
                // Format: server_address: (empty after colon)
                // Use default port and path
            } else if (after_colon[0] == '/' || after_colon[0] == '\\' || 
                      (after_colon.length() > 1 && after_colon[1] == ':')) {
                // Format: server_address:/path or server_address:\path or server_address:C:\path
                remote_path = after_colon;
            } else {
                // Check if it's a port number or path
                // Look for path separator to determine where port ends
                size_t slash_pos = after_colon.find_first_of("/\\");
                size_t colon_pos = after_colon.find(':');
                
                // Handle cases like "1245:D:/Work/" (invalid format)
                if (colon_pos != std::string::npos && colon_pos < slash_pos) {
                    std::cerr << "Error: Invalid destination format. Multiple colons detected." << std::endl;
                    std::cerr << "Use: server:port/path  (e.g., 127.0.0.1:1245/D:/Work/)" << std::endl;
                    std::cerr << "Or:  server:path       (e.g., 127.0.0.1:D:/Work/)" << std::endl;
                    return 1;
                }
                
                std::string potential_port = (slash_pos != std::string::npos) ? 
                    after_colon.substr(0, slash_pos) : after_colon;
                
                // Try to parse as port number
                if (server_port == 0) { // Only parse port if not provided via command line
                    try {
                        int port_int = std::stoi(potential_port);
                        if (port_int >= 1 && port_int <= 65535) {
                            server_port = static_cast<uint16_t>(port_int);
                            if (slash_pos != std::string::npos) {
                                remote_path = after_colon.substr(slash_pos);
                            }
                        } else {
                            // Not a valid port, treat as path
                            remote_path = after_colon;
                        }
                    } catch (const std::exception&) {
                        // Not a number, treat as path
                        remote_path = after_colon;
                    }
                } else {
                    // Port already specified via command line, treat as path
                    remote_path = after_colon;
                }
            }
        }
        
        // Validate server address
        if (server_address.empty()) {
            std::cerr << "Error: Missing server address" << std::endl;
            std::cerr << "Usage: " << argv[0] << " [options] <source> <destination>" << std::endl;
            std::cerr << "Destination formats:" << std::endl;
            std::cerr << "  server_address:port/path  (e.g., 127.0.0.1:1245/remote/path)" << std::endl;
            std::cerr << "  server_address:/path      (e.g., 127.0.0.1:/remote/path, uses default port)" << std::endl;
            std::cerr << "  server_address            (e.g., 127.0.0.1, uses default port and path)" << std::endl;
            return 1;
        }
        
        // Set default port if not specified
        if (server_port == 0) {
            server_port = 1245; // Default port
        }
        
        // Normalize remote path based on detected format
        if (!remote_path.empty() && remote_path != "/") {
            // Detect if this is a Windows absolute path (e.g., C:\, D:\)
            if (netcopy::common::is_absolute_path(remote_path)) {
                // Keep absolute paths as-is but convert to Unix style for network transmission
                remote_path = netcopy::common::convert_to_unix_path(remote_path);
            } else {
                // For relative paths, ensure they start with /
                remote_path = netcopy::common::convert_to_unix_path(remote_path);
                if (remote_path[0] != '/') {
                    remote_path = "/" + remote_path;
                }
            }
        }
        
        // Debug output for path handling
        if (args.verbose) {
            std::cout << "Platform: " << (netcopy::common::is_windows_platform() ? "Windows" : "Unix") << std::endl;
            std::cout << "Remote path (network format): " << remote_path << std::endl;
            std::cout << "Remote path (native format): " << netcopy::common::convert_to_native_path(remote_path) << std::endl;
        }

        // Set security level before connecting
        client.set_security_level(args.security_level);
        if (args.verbose) {
            std::string level_name;
            switch (args.security_level) {
                case netcopy::crypto::SecurityLevel::HIGH:
                    level_name = "HIGH (ChaCha20-Poly1305)";
                    break;
                case netcopy::crypto::SecurityLevel::FAST:
                    level_name = "FAST (XOR cipher)";
                    break;
                case netcopy::crypto::SecurityLevel::AES:
                    level_name = "AES (AES-CTR with hardware acceleration)";
                    // Show detailed AES acceleration information
                    std::cout << netcopy::crypto::AesCtr::get_detailed_acceleration_info() << std::endl;
                    break;
                case netcopy::crypto::SecurityLevel::AES_256_GCM:
                    level_name = "AES-256-GCM (GPU accelerated)";
                    // Show detailed GPU acceleration information
                    std::cout << netcopy::crypto::Aes256GcmGpu::get_detailed_gpu_info() << std::endl;
                    break;
            }
            std::cout << "Security level: " << level_name << std::endl;
            std::cout << "Connecting to " << server_address << ":" << server_port << std::endl;
        }
        client.connect(server_address, server_port);
        if (args.verbose) {
            std::cout << "Connected successfully" << std::endl;
        }

        // Create bandwidth monitor
        auto bandwidth_monitor = std::make_shared<netcopy::common::BandwidthMonitor>();
        uint64_t last_bytes = 0;
        
        client.set_progress_callback(static_cast<netcopy::client::Client::ProgressCallback>([bandwidth_monitor, &last_bytes](uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file) {
            if (total_bytes > 0) {
                // Record new bytes for bandwidth calculation
                uint64_t new_bytes = bytes_transferred - last_bytes;
                if (new_bytes > 0) {
                    bandwidth_monitor->record_bytes(new_bytes);
                    last_bytes = bytes_transferred;
                }
                
                double progress = static_cast<double>(bytes_transferred) / total_bytes * 100.0;
                std::string filename = netcopy::file::FileManager::get_filename(current_file);
                
                // Format file size in human readable format
                auto format_size = [](uint64_t bytes) -> std::string {
                    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
                    int unit = 0;
                    double size = static_cast<double>(bytes);
                    while (size >= 1024 && unit < 4) {
                        size /= 1024;
                        unit++;
                    }
                    char buffer[32];
                    if (unit == 0) {
                        snprintf(buffer, sizeof(buffer), "%.0f %s", size, units[unit]);
                    } else {
                        snprintf(buffer, sizeof(buffer), "%.1f %s", size, units[unit]);
                    }
                    return std::string(buffer);
                };
                
                // Get current transfer rate
                std::string rate_str = bandwidth_monitor->get_rate_string();
                
                std::cout << "\r" << filename << ": " << std::fixed << std::setprecision(1) << progress << "% "
                          << "(" << format_size(bytes_transferred) << "/" << format_size(total_bytes) << ") "
                          << "at " << rate_str << std::flush;
            }
        }));

        if (netcopy::file::FileManager::is_directory(args.source_path)) {
            if (!args.recursive) {
                throw std::runtime_error("Cannot transfer directory without -R/--recursive flag. Use -R to transfer directories recursively.");
            }
            std::string source_name = netcopy::file::FileManager::get_filename(args.source_path);
            std::cout << "Transferring directory: " << source_name << std::endl;
            client.transfer_directory(args.source_path, remote_path, args.recursive, args.resume);
            std::cout << std::endl << "Directory transfer completed: " << source_name << std::endl;
        } else {
            std::string filename = netcopy::file::FileManager::get_filename(args.source_path);
            std::cout << "Transferring file: " << filename << std::endl;
            client.transfer_file(args.source_path, remote_path, args.resume);
            std::cout << std::endl << "File transfer completed: " << filename << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}


