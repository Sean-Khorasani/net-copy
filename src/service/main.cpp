#ifdef _WIN32
#include "service/windows_service.h"
#include "common/utils.h"
#include <iostream>
#include <string>
#include <filesystem>

void print_usage(const char* program_name) {
    std::cout << "NetCopy Service Manager - Windows Service Control" << std::endl;
    std::cout << netcopy::common::get_version_string() << std::endl;
    std::cout << netcopy::common::get_build_info() << std::endl << std::endl;
    
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program_name << " [command]" << std::endl << std::endl;
    
    std::cout << "Commands:" << std::endl;
    std::cout << "  install     Install NetCopy as Windows service" << std::endl;
    std::cout << "  uninstall   Uninstall NetCopy Windows service" << std::endl;
    std::cout << "  start       Start NetCopy service" << std::endl;
    std::cout << "  stop        Stop NetCopy service" << std::endl;
    std::cout << "  status      Show service status" << std::endl;
    std::cout << "  run         Run service (used by Windows Service Manager)" << std::endl;
    std::cout << "  help        Show this help message" << std::endl << std::endl;
    
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " install" << std::endl;
    std::cout << "  " << program_name << " start" << std::endl;
    std::cout << "  " << program_name << " stop" << std::endl;
    std::cout << "  " << program_name << " uninstall" << std::endl;
}

std::string get_service_executable_path() {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return std::string(path);
}

int service_main_entry(int argc, char* argv[]) {
    const std::string SERVICE_NAME = "NetCopyServer";
    const std::string DISPLAY_NAME = "NetCopy File Transfer Server";
    
    netcopy::service::WindowsService service(SERVICE_NAME, DISPLAY_NAME);
    
    if (argc >= 2 && std::string(argv[1]) == "--version") {
        std::cout << netcopy::common::get_version_string() << std::endl;
        return 0;
    }
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    try {
        if (command == "install") {
            std::string exe_path = get_service_executable_path();
            std::string service_cmd = "\"" + exe_path + "\" run";
            
            if (service.install_service(service_cmd)) {
                std::cout << "Service installed successfully." << std::endl;
                std::cout << "Use 'net start " << SERVICE_NAME << "' or '" << argv[0] << " start' to start the service." << std::endl;
                return 0;
            } else {
                std::cerr << "Failed to install service." << std::endl;
                return 1;
            }
        }
        else if (command == "uninstall") {
            if (service.uninstall_service()) {
                std::cout << "Service uninstalled successfully." << std::endl;
                return 0;
            } else {
                std::cerr << "Failed to uninstall service." << std::endl;
                return 1;
            }
        }
        else if (command == "start") {
            if (service.start_service()) {
                std::cout << "Service started successfully." << std::endl;
                return 0;
            } else {
                std::cerr << "Failed to start service." << std::endl;
                return 1;
            }
        }
        else if (command == "stop") {
            if (service.stop_service()) {
                std::cout << "Service stopped successfully." << std::endl;
                return 0;
            } else {
                std::cerr << "Failed to stop service." << std::endl;
                return 1;
            }
        }
        else if (command == "status") {
            if (service.is_service_running()) {
                std::cout << "Service is running." << std::endl;
                return 0;
            } else {
                std::cout << "Service is not running." << std::endl;
                return 1;
            }
        }
        else if (command == "run") {
            // This is called by Windows Service Manager
            std::cout << "Starting NetCopy service..." << std::endl;
            service.run_service();
            return 0;
        }
        else if (command == "help" || command == "-h" || command == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else {
            std::cerr << "Unknown command: " << command << std::endl;
            std::cerr << "Use '" << argv[0] << " help' for usage information." << std::endl;
            return 1;
        }
    }
    catch (const std::exception& e) {
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
    return service_main_entry(argc, utf8_argv.data());
}
#else
int main(int argc, char* argv[]) {
    return service_main_entry(argc, argv);
}
#endif

#else
#include "common/utils.h"
int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "--version") {
        std::cout << netcopy::common::get_version_string() << std::endl;
        return 0;
    }
    std::cout << "Windows service functionality is only available on Windows." << std::endl;
    return 1;
}
#endif // _WIN32