#include "client/client.h"
#include "common/utils.h"
#include "logging/logger.h"
#include "file/file_manager.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/aes_ctr.h"
#include "crypto/sha3.h"
#include "crypto/aes_256_gcm_gpu.h"
#include "common/bandwidth_monitor.h"
#include <iostream>
#include <string>
#include <iomanip>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <csignal>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
netcopy::client::Client* g_active_client = nullptr;

#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        if (g_active_client != nullptr) {
            g_active_client->request_cancel();
            return TRUE;
        }
    }
    return FALSE;
}
#else
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        if (g_active_client != nullptr) {
            g_active_client->request_cancel();
        }
    }
}
#endif

enum class OverwriteStateEnum {
    ASK,
    OVERWRITE_ALL,
    SKIP_ALL
};

struct OverwriteState {
    std::mutex mutex;
    OverwriteStateEnum state = OverwriteStateEnum::ASK;
};

struct ProgressState {
    std::mutex mutex;
    std::unordered_map<std::string, int> file_to_slot;
    std::vector<bool> slot_occupied;
    int max_slot_used = -1;
    std::unordered_map<std::string, uint64_t> last_bytes_map;
    std::vector<size_t> slot_line_lengths;
    std::unordered_set<std::string> started_files;
    std::unordered_set<std::string> completed_files;
};
}

struct CommandLineArgs {
    std::string config_file;
    std::string source_path;
    std::string destination_path;
    bool recursive = false;
    bool resume = false;
    bool verbose = false;
    bool help = false;
    bool download = false;
    uint16_t server_port = 0; // Added for client port
    bool empty_dirs_specified = false; // Track if user specified --no-empty-dirs
    bool create_empty_directories = true; // Default value
    bool auto_create_directories = true;
    bool auto_create_specified = false;
    netcopy::crypto::SecurityLevel security_level = netcopy::crypto::SecurityLevel::HIGH;
    bool force = false;
    bool version = false;
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
    std::cout << "  --auto-create              Automatically create non-existent directories (default)" << std::endl;
    std::cout << "  --no-auto-create           Disable automatic directory creation" << std::endl;
    std::cout << "  --no-empty-dirs            Don't create empty directories" << std::endl;
    std::cout << "  -s, --security LEVEL       Security level: high (default), fast, aes, or AES-256-GCM" << std::endl;
    std::cout << "  -g, --get, --download      Download/pull file/directory from server" << std::endl;
    std::cout << "  -f, --force                Force replacing existing files/folders without prompting" << std::endl;
    std::cout << "  -v, --verbose              Enable verbose logging" << std::endl;
    std::cout << "  -h, --help                 Show this help message" << std::endl << std::endl;
    
    std::cout << "Destination formats (Source formats if downloading):" << std::endl;
    std::cout << "  server:port/path           e.g., 127.0.0.1:1245/D:/Work/" << std::endl;
    std::cout << "  server:path                e.g., 127.0.0.1:D:\\Work\\          (port from config/default)" << std::endl;
    std::cout << "  server:/path               e.g., 127.0.0.1:/D:/Work/ (uses default/config port)" << std::endl;
    std::cout << "  server                     e.g., 127.0.0.1 (uses default port and path)" << std::endl << std::endl;
    
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " file.txt 127.0.0.1:1245/D:/Work/" << std::endl;
    std::cout << "  " << program_name << " file.txt 127.0.0.1:D:\\Work\\" << std::endl;
    std::cout << "  " << program_name << " -p 1245 file.txt 127.0.0.1:/D:/Work/" << std::endl;
    std::cout << "  " << program_name << " -R folder/ 127.0.0.1" << std::endl;
    std::cout << "  " << program_name << " ./folder/ 192.168.1.100:/remote/path/ -R" << std::endl;
    std::cout << "  " << program_name << " large_file.zip 192.168.1.100:/downloads/ --resume" << std::endl;
    std::cout << "  " << program_name << " --get 192.168.1.100:/remote/file.txt ./local_file.txt" << std::endl;
    std::cout << "  " << program_name << " --get -R 192.168.1.100:/remote/dir ./local_dir" << std::endl;
}

CommandLineArgs parse_arguments(int argc, char* argv[]) {
    CommandLineArgs args;
    
    std::vector<std::string> positional_args;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.help = true;
            return args;
        } else if (arg == "--version") {
            args.version = true;
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
        } else if (arg == "-f" || arg == "--force") {
            args.force = true;
        } else if (arg == "--auto-create") {
            args.auto_create_directories = true;
            args.auto_create_specified = true;
        } else if (arg == "--no-auto-create") {
            args.auto_create_directories = false;
            args.auto_create_specified = true;
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
        } else if (arg == "-g" || arg == "--get" || arg == "--download") {
            args.download = true;
        } else {
            positional_args.push_back(arg);
        }
    }
    
    if (positional_args.size() == 2) {
        args.source_path = positional_args[0];
        args.destination_path = positional_args[1];
    } else if (!args.help && !args.version) {
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

int client_main(int argc, char* argv[]) {
    try {
        auto args = parse_arguments(argc, argv);
        
        if (args.version) {
            std::cout << netcopy::common::get_version_string() << std::endl;
            return 0;
        }
        
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

        if (args.auto_create_specified) {
            config.auto_create_directories = args.auto_create_directories;
            if (args.verbose) {
                std::cout << "Command line override: auto_create_directories = " << 
                            (config.auto_create_directories ? "true" : "false") << std::endl;
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

        // Decrypt password_encrypted if set and password is not already set
        if (!config.password_encrypted.empty() && config.password.empty()) {
            std::string master = config.private_key_passphrase;
            if (master.empty()) {
                master = netcopy::common::get_password_from_console("Enter passphrase to decrypt stored password: ");
            }
            std::vector<uint8_t> salt = {0x6e,0x63,0x70,0x77,0x64,0x73,0x61,0x6c,
                                          0x74,0x30,0x31,0x32,0x33,0x34,0x35,0x36};
            auto dk = netcopy::crypto::pbkdf2_sha3_256(master, salt, 100000, 64);
            std::vector<uint8_t> aes_key(dk.begin(), dk.begin() + 32);
            std::vector<uint8_t> hmac_key(dk.begin() + 32, dk.end());

            auto blob = netcopy::crypto::base64_decode(config.password_encrypted);
            if (blob.size() > 32) {
                std::vector<uint8_t> stored_mac(blob.begin(), blob.begin() + 32);
                std::vector<uint8_t> ciphertext(blob.begin() + 32, blob.end());
                auto expected_mac = netcopy::crypto::hmac_sha3_256(hmac_key, ciphertext);
                if (expected_mac != stored_mac) {
                    throw std::runtime_error("Wrong passphrase or corrupted password_encrypted value");
                }
                
                netcopy::crypto::AesCtr::Key key_arr;
                std::copy(aes_key.begin(), aes_key.end(), key_arr.begin());
                std::vector<uint8_t> iv(hmac_key.begin(), hmac_key.begin() + 16);
                netcopy::crypto::AesCtr::IV iv_arr;
                std::copy(iv.begin(), iv.end(), iv_arr.begin());
                
                netcopy::crypto::AesCtr cipher(key_arr);
                auto plaintext = cipher.process(ciphertext, iv_arr);
                config.password = std::string(plaintext.begin(), plaintext.end());
            }
        }

        client.set_config(config);
        
        // Reconfigure logging with the updated settings
        auto& logger = netcopy::logging::Logger::instance();
        logger.set_level(netcopy::logging::Logger::string_to_level(config.log_level));
        logger.set_console_output(config.console_output);
        logger.set_json_format(config.log_format == "json");
        if (!config.log_file.empty()) {
            logger.set_file_output(config.log_file);
        }
        
        // Log configuration loaded message only in verbose mode (after logger is reconfigured)
        if (args.verbose) {
            std::cout << "Client configuration loaded from: " << config_path_used << std::endl;
        }

        // Parse destination (or source if downloading) - support multiple formats:
        // 1. server_address:port/path  (e.g., 127.0.0.1:1245/path)
        // 2. server_address:path       (e.g., 127.0.0.1:/path, uses port from config/command line)
        // 3. server_address            (e.g., 127.0.0.1, uses port from config/command line and default path)
        
        std::string server_address;
        uint16_t server_port = args.server_port; // Use command line port if provided
        std::string remote_path = "/"; // Default path
        
        std::string path_to_parse = args.download ? args.source_path : args.destination_path;
        std::string local_path = args.download ? args.destination_path : args.source_path;
        
        if (!path_to_parse.empty() && path_to_parse.front() == '[') {
            size_t close_bracket = path_to_parse.find(']');
            if (close_bracket == std::string::npos) {
                std::cerr << "Error: Invalid bracketed IPv6 destination. Missing ']'" << std::endl;
                return 1;
            }
            server_address = path_to_parse.substr(1, close_bracket - 1);
            std::string after_bracket = path_to_parse.substr(close_bracket + 1);
            if (after_bracket.empty()) {
                // Format: [::1] (uses default port and path)
            } else if (after_bracket[0] == ':') {
                std::string after_colon = after_bracket.substr(1);
                if (after_colon.empty()) {
                    // Format: [::1]:
                    // Use default port and path
                } else if (after_colon[0] == '/' || after_colon[0] == '\\' || 
                           (after_colon.length() > 1 && after_colon[1] == ':')) {
                    // Format: [::1]:/path or [::1]:D:/path
                    remote_path = after_colon;
                } else {
                    size_t slash_pos = after_colon.find_first_of("/\\");
                    std::string potential_port = (slash_pos != std::string::npos) ? 
                        after_colon.substr(0, slash_pos) : after_colon;
                    
                    if (server_port == 0) {
                        try {
                            int port_int = std::stoi(potential_port);
                            if (port_int >= 1 && port_int <= 65535) {
                                server_port = static_cast<uint16_t>(port_int);
                                if (slash_pos != std::string::npos) {
                                    remote_path = after_colon.substr(slash_pos);
                                }
                            } else {
                                remote_path = after_colon;
                            }
                        } catch (const std::exception&) {
                            remote_path = after_colon;
                        }
                    } else {
                        remote_path = after_colon;
                    }
                }
            } else {
                // Format: [::1]/path
                remote_path = after_bracket;
            }
        } else {
            size_t colon_pos = path_to_parse.find(":");
            if (colon_pos == std::string::npos) {
                // Format: server_address (no port, no path)
                server_address = path_to_parse;
            } else {
                server_address = path_to_parse.substr(0, colon_pos);
                std::string after_colon = path_to_parse.substr(colon_pos + 1);
                
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
                    size_t colon_pos_inner = after_colon.find(':');
                    
                    // Handle cases like "1245:D:/Work/" (invalid format)
                    if (colon_pos_inner != std::string::npos && colon_pos_inner < slash_pos) {
                        std::cerr << "Error: Invalid remote format. Multiple colons detected." << std::endl;
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
        }
        
        // Validate server address
        if (server_address.empty()) {
            std::cerr << "Error: Missing server address" << std::endl;
            std::cerr << "Usage: " << argv[0] << " [options] <source> <destination>" << std::endl;
            std::cerr << "Destination formats (Source formats if downloading):" << std::endl;
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
        if (args.security_level == netcopy::crypto::SecurityLevel::FAST) {
            std::cerr << "WARNING: 'fast' mode uses XOR cipher which provides NO real security. Use only on trusted local networks." << std::endl;
        }

        client.connect(server_address, server_port);
        if (args.verbose) {
            std::cout << "Connected successfully" << std::endl;
        }

#ifdef _WIN32
        g_active_client = &client;
        SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
        g_active_client = &client;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
#endif

        bool use_ansi = false;
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                if (SetConsoleMode(hOut, dwMode)) {
                    use_ansi = true;
                }
            }
        }
#else
        use_ansi = (isatty(fileno(stdout)) != 0);
#endif

        // Create overwrite state
        auto overwrite_state = std::make_shared<OverwriteState>();
        
        client.set_overwrite_callback(static_cast<netcopy::client::Client::OverwriteCallback>([overwrite_state, args](const std::string& remote_path, uint64_t remote_size) {
            if (args.force) {
                return netcopy::client::Client::OverwriteDecision::OVERWRITE;
            }
            
            std::lock_guard<std::mutex> lock(overwrite_state->mutex);
            if (overwrite_state->state == OverwriteStateEnum::OVERWRITE_ALL) {
                return netcopy::client::Client::OverwriteDecision::OVERWRITE;
            }
            if (overwrite_state->state == OverwriteStateEnum::SKIP_ALL) {
                return netcopy::client::Client::OverwriteDecision::SKIP;
            }
            
            std::cout << "\nFile already exists: " << remote_path << " (" << remote_size << " bytes)" << std::endl;
            while (true) {
                std::cout << "Overwrite? [y]es, [n]o, [a]ll, [s]kip all, [c]ancel: " << std::flush;
                std::string response;
                if (!std::getline(std::cin, response)) {
                    return netcopy::client::Client::OverwriteDecision::CANCEL;
                }
                // Trim and convert to lower case
                std::transform(response.begin(), response.end(), response.begin(), ::tolower);
                if (response == "y" || response == "yes") {
                    return netcopy::client::Client::OverwriteDecision::OVERWRITE;
                } else if (response == "n" || response == "no") {
                    return netcopy::client::Client::OverwriteDecision::SKIP;
                } else if (response == "a" || response == "all") {
                    overwrite_state->state = OverwriteStateEnum::OVERWRITE_ALL;
                    return netcopy::client::Client::OverwriteDecision::OVERWRITE;
                } else if (response == "s" || response == "skip all") {
                    overwrite_state->state = OverwriteStateEnum::SKIP_ALL;
                    return netcopy::client::Client::OverwriteDecision::SKIP;
                } else if (response == "c" || response == "cancel") {
                    return netcopy::client::Client::OverwriteDecision::CANCEL;
                }
            }
        }));

        // Create bandwidth monitor
        auto bandwidth_monitor = std::make_shared<netcopy::common::BandwidthMonitor>();
        
        auto state = std::make_shared<ProgressState>();
        
        client.set_progress_callback(static_cast<netcopy::client::Client::ProgressCallback>([bandwidth_monitor, state, use_ansi, args, &client, overwrite_state](uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file) {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (total_bytes == 0) return;

            std::unique_lock<std::mutex> prompt_lock(overwrite_state->mutex, std::try_to_lock);
            if (!prompt_lock.owns_lock()) {
                // A prompt is currently active! Skip rendering progress to prevent screen corruption.
                return;
            }

            // Track bandwidth
            uint64_t prev_bytes = state->last_bytes_map[current_file];
            uint64_t new_bytes = 0;
            if (bytes_transferred > prev_bytes) {
                new_bytes = bytes_transferred - prev_bytes;
                state->last_bytes_map[current_file] = bytes_transferred;
            }
            if (new_bytes > 0) {
                bandwidth_monitor->record_bytes(new_bytes);
            }

            std::string filename = netcopy::file::FileManager::get_filename(current_file);
            double progress = static_cast<double>(bytes_transferred) / total_bytes * 100.0;

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

            std::string rate_str = bandwidth_monitor->get_rate_string();
            std::ostringstream line;
            line << filename << ": " << std::fixed << std::setprecision(1) << progress << "% "
                 << "(" << format_size(bytes_transferred) << "/" << format_size(total_bytes) << ") "
                 << "at " << rate_str;
            std::string output = line.str();

            bool is_concurrent = args.recursive && !args.download && (client.get_negotiated_parallel_streams() > 1);

            if (use_ansi) {
                if (is_concurrent) {
                    // Get or assign slot
                    int slot = -1;
                    auto it = state->file_to_slot.find(current_file);
                    if (it != state->file_to_slot.end()) {
                        slot = it->second;
                    } else {
                        // Find first unoccupied slot
                        for (size_t i = 0; i < state->slot_occupied.size(); ++i) {
                            if (!state->slot_occupied[i]) {
                                slot = static_cast<int>(i);
                                break;
                            }
                        }
                        if (slot == -1) {
                            slot = static_cast<int>(state->slot_occupied.size());
                            state->slot_occupied.push_back(true);
                            state->slot_line_lengths.push_back(0);
                        } else {
                            state->slot_occupied[slot] = true;
                        }
                        state->file_to_slot[current_file] = slot;
                        
                        // If we need to expand terminal lines
                        if (slot > state->max_slot_used) {
                            int lines_to_add = slot - state->max_slot_used;
                            for (int l = 0; l < lines_to_add; ++l) {
                                std::cout << "\n";
                            }
                            state->max_slot_used = slot;
                        }
                    }

                    size_t prev_len = state->slot_line_lengths[slot];
                    if (output.size() < prev_len) {
                        output.append(prev_len - output.size(), ' ');
                    }
                    state->slot_line_lengths[slot] = output.size();

                    // Draw to slot
                    int N = state->max_slot_used + 1;
                    int lines_up = N - slot;
                    std::cout << "\033[" << lines_up << "A\r\033[K" << output << "\033[" << lines_up << "B\r" << std::flush;

                    // If completed, release slot
                    if (bytes_transferred >= total_bytes) {
                        state->slot_occupied[slot] = false;
                        state->file_to_slot.erase(current_file);
                    }
                } else {
                    // Sequential with ANSI support
                    std::cout << "\r\033[K" << output << std::flush;
                    if (bytes_transferred >= total_bytes) {
                        std::cout << "\n";
                    }
                }
            } else {
                // Non-ANSI fallback (e.g. redirected output, logs)
                if (state->started_files.find(current_file) == state->started_files.end()) {
                    std::cout << "Transferring " << filename << " (" << format_size(total_bytes) << ")..." << std::endl;
                    state->started_files.insert(current_file);
                }
                if (bytes_transferred >= total_bytes && state->completed_files.find(current_file) == state->completed_files.end()) {
                    std::cout << "Completed " << filename << " (" << format_size(total_bytes) << ") at " << rate_str << std::endl;
                    state->completed_files.insert(current_file);
                }
            }
        }));

        if (args.download) {
            bool is_dir = false;
            try {
                // Try listing the path to see if it is a directory
                client.list_remote_directory(remote_path, false);
                is_dir = true;
            } catch (const std::exception&) {
                // Assume it's a file or not found (download_file will handle file-not-found)
                is_dir = false;
            }

            if (is_dir) {
                if (!args.recursive) {
                    throw std::runtime_error("Cannot transfer directory without -R/--recursive flag. Use -R to transfer directories recursively.");
                }
                std::string remote_name = remote_path;
                size_t last_slash = remote_path.find_last_of("/\\");
                if (last_slash != std::string::npos) {
                    remote_name = remote_path.substr(last_slash + 1);
                }
                std::cout << "Downloading directory: " << remote_name << std::endl;
                client.download_directory(remote_path, local_path, args.recursive);
                std::cout << std::endl << "Directory download completed: " << remote_name << std::endl;
            } else {
                std::string filename = remote_path;
                size_t last_slash = remote_path.find_last_of("/\\");
                if (last_slash != std::string::npos) {
                    filename = remote_path.substr(last_slash + 1);
                }
                std::cout << "Downloading file: " << filename << std::endl;
                client.download_file(remote_path, local_path);
                std::cout << std::endl << "File download completed: " << filename << std::endl;
            }
        } else {
            if (netcopy::file::FileManager::is_directory(local_path)) {
                if (!args.recursive) {
                    throw std::runtime_error("Cannot transfer directory without -R/--recursive flag. Use -R to transfer directories recursively.");
                }
                std::string source_name = netcopy::file::FileManager::get_filename(local_path);
                std::cout << "Transferring directory: " << source_name << std::endl;
                client.transfer_directory(local_path, remote_path, args.recursive, args.resume);
                std::cout << std::endl << "Directory transfer completed: " << source_name << std::endl;
            } else {
                std::string filename = netcopy::file::FileManager::get_filename(local_path);
                std::cout << "Transferring file: " << filename << std::endl;
                client.transfer_file(local_path, remote_path, args.resume);
                std::cout << std::endl << "File transfer completed: " << filename << std::endl;
            }
        }

#ifdef _WIN32
        SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
        g_active_client = nullptr;
#endif
        
    } catch (const std::exception& e) {
#ifdef _WIN32
        SetConsoleCtrlHandler(console_ctrl_handler, FALSE);
        g_active_client = nullptr;
#endif
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
    return client_main(argc, utf8_argv.data());
}
#else
int main(int argc, char* argv[]) {
    return client_main(argc, argv);
}
#endif
