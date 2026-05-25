#include "gui/gui_server.h"
#include "gui/index_html.h"
#include "file/file_manager.h"
#include "common/utils.h"
#include "exceptions.h"
#include "logging/logger.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <filesystem>
#include <cstring>
#include <cstdio>
#include <iomanip>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SOCKET_TYPE SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#define SOCKET_TYPE int
#define INVALID_SOCKET -1
#endif

namespace netcopy {
namespace gui {

namespace {
std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (static_cast<unsigned char>(c) < 32) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

std::string normalize_gui_path(const std::string& path) {
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    for (auto& c : norm) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    return norm;
}

std::string format_rate(double bytes_per_sec) {
    if (bytes_per_sec < 0) bytes_per_sec = 0;
    if (bytes_per_sec < 1024) {
        return std::to_string(static_cast<int>(bytes_per_sec)) + " B/s";
    } else if (bytes_per_sec < 1024 * 1024) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f KB/s", bytes_per_sec / 1024.0);
        return buf;
    } else if (bytes_per_sec < 1024 * 1024 * 1024) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f MB/s", bytes_per_sec / (1024.0 * 1024.0));
        return buf;
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f GB/s", bytes_per_sec / (1024.0 * 1024.0 * 1024.0));
        return buf;
    }
}
} // namespace

GuiServer::GuiServer()
    : port_(0),
      running_(false),
      listen_socket_(static_cast<uint64_t>(INVALID_SOCKET)),
      active_security_level_(crypto::SecurityLevel::HIGH),
      remote_connected_(false),
      remote_port_(0),
      transfer_active_(false),
      transfer_bytes_(0),
      transfer_total_bytes_(0),
      transfer_rate_(0.0) {}

GuiServer::~GuiServer() {
    stop();
}

bool GuiServer::start(uint16_t port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed.");
        return false;
    }
#endif

    SOCKET_TYPE s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("Failed to create listen socket.");
        return false;
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    bool bound = false;
    uint16_t current_port = port;
    // Auto-increment fallback if port is busy (try up to 50 ports)
    for (int i = 0; i < 50; ++i) {
        addr.sin_port = htons(current_port);
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            bound = true;
            break;
        }
        current_port++;
    }

    if (!bound) {
        LOG_ERROR("Failed to bind to port " + std::to_string(port) + " or any fallback ports.");
        closesocket(s);
        return false;
    }

    if (listen(s, SOMAXCONN) != 0) {
        LOG_ERROR("Failed to listen on socket.");
        closesocket(s);
        return false;
    }

    listen_socket_ = static_cast<uint64_t>(s);
    port_ = current_port;
    running_ = true;

    listen_thread_ = std::thread(&GuiServer::listen_loop, this);
    return true;
}

void GuiServer::stop() {
    running_ = false;
    SOCKET_TYPE s = static_cast<SOCKET_TYPE>(listen_socket_);
    if (s != INVALID_SOCKET) {
        closesocket(s);
        listen_socket_ = static_cast<uint64_t>(INVALID_SOCKET);
    }

    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        for (auto& pair : transfers_) {
            if (pair.second->client_inst) {
                pair.second->client_inst->request_cancel();
                pair.second->client_inst->disconnect();
            }
            if (pair.second->thread.joinable()) {
                pair.second->thread.join();
            }
        }
        transfers_.clear();
    }

    std::lock_guard<std::mutex> lock(client_mutex_);
    if (active_client_) {
        active_client_->disconnect();
        active_client_.reset();
    }
    remote_connected_ = false;

#ifdef _WIN32
    WSACleanup();
#endif
}

void GuiServer::listen_loop() {
    while (running_) {
        SOCKET_TYPE s = static_cast<SOCKET_TYPE>(listen_socket_);
        if (s == INVALID_SOCKET) {
            break;
        }

        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET_TYPE client_sock = accept(s, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client_sock == INVALID_SOCKET) {
            if (!running_) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        std::thread(&GuiServer::handle_client, this, static_cast<uint64_t>(client_sock)).detach();
    }
}

void GuiServer::handle_client(uint64_t client_socket) {
    std::string request_data;
    char buffer[4096];
    int bytes_received = 0;
    size_t header_end = std::string::npos;
    SOCKET_TYPE sock = static_cast<SOCKET_TYPE>(client_socket);

    while (true) {
        bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            closesocket(sock);
            return;
        }
        buffer[bytes_received] = '\0';
        request_data.append(buffer, bytes_received);

        header_end = request_data.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;
        }
        if (request_data.length() > 65536) {
            send_error(client_socket, 400, "Bad Request: Headers too large");
            closesocket(sock);
            return;
        }
    }

    std::string headers = request_data.substr(0, header_end);
    std::string body = request_data.substr(header_end + 4);

    size_t first_line_end = headers.find("\r\n");
    if (first_line_end == std::string::npos) {
        send_error(client_socket, 400, "Bad Request");
        closesocket(sock);
        return;
    }
    std::string request_line = headers.substr(0, first_line_end);

    size_t method_end = request_line.find(' ');
    if (method_end == std::string::npos) {
        send_error(client_socket, 400, "Bad Request");
        closesocket(sock);
        return;
    }
    std::string method = request_line.substr(0, method_end);

    size_t path_end = request_line.find(' ', method_end + 1);
    if (path_end == std::string::npos) {
        send_error(client_socket, 400, "Bad Request");
        closesocket(sock);
        return;
    }
    std::string full_path = request_line.substr(method_end + 1, path_end - (method_end + 1));

    std::string path = full_path;
    std::string query_string;
    size_t q_pos = full_path.find('?');
    if (q_pos != std::string::npos) {
        path = full_path.substr(0, q_pos);
        query_string = full_path.substr(q_pos + 1);
    }

    size_t content_length = 0;
    size_t cl_pos = headers.find("Content-Length:");
    if (cl_pos == std::string::npos) {
        cl_pos = headers.find("content-length:");
    }
    if (cl_pos != std::string::npos) {
        size_t cl_val_start = headers.find_first_not_of(" \t", cl_pos + 15);
        size_t cl_val_end = headers.find("\r\n", cl_val_start);
        if (cl_val_start != std::string::npos && cl_val_end != std::string::npos) {
            content_length = std::stoull(headers.substr(cl_val_start, cl_val_end - cl_val_start));
        }
    }

    while (body.length() < content_length) {
        bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            closesocket(sock);
            return;
        }
        buffer[bytes_received] = '\0';
        body.append(buffer, bytes_received);
    }
    if (body.length() > content_length) {
        body = body.substr(0, content_length);
    }

    if (method == "OPTIONS") {
        send_response(client_socket, "204 No Content", "text/plain", "");
    } else if (method == "GET" && (path == "/" || path == "/index.html")) {
        send_response(client_socket, "200 OK", "text/html", GUI_HTML_CONTENT);
    } else if (method == "GET" && path == "/api/drives") {
        handle_api_drives(client_socket);
    } else if (method == "GET" && path == "/api/local/list") {
        handle_api_local_list(client_socket, query_string);
    } else if (method == "POST" && path == "/api/connect") {
        handle_api_connect(client_socket, body);
    } else if (method == "POST" && path == "/api/disconnect") {
        handle_api_disconnect(client_socket);
    } else if (method == "GET" && path == "/api/remote/list") {
        handle_api_remote_list(client_socket, query_string);
    } else if (method == "POST" && path == "/api/transfer") {
        handle_api_transfer(client_socket, body);
    } else if (method == "GET" && path == "/api/transfers") {
        handle_api_transfers(client_socket);
    } else if (method == "GET" && path == "/api/transfer/status") {
        handle_api_transfer_status(client_socket, query_string);
    } else if (method == "POST" && path == "/api/transfer/remove") {
        handle_api_transfer_remove(client_socket, body);
    } else if (method == "POST" && path == "/api/transfer/control") {
        handle_api_transfer_control(client_socket, body);
    } else if (method == "POST" && path == "/api/transfer/file/control") {
        handle_api_transfer_file_control(client_socket, body);
    } else if (method == "GET" && path == "/api/transfer/server_session") {
        handle_api_transfer_server_session(client_socket, query_string);
    } else if (method == "GET" && path == "/api/remote/check") {
        handle_api_remote_check(client_socket);
    } else if (method == "POST" && path == "/api/local/create_dir") {
        handle_api_local_create_dir(client_socket, query_string);
    } else if (method == "POST" && path == "/api/remote/create_dir") {
        handle_api_remote_create_dir(client_socket, query_string);
    } else {
        send_error(client_socket, 404, "Not Found");
    }

    closesocket(sock);
}

void GuiServer::send_response(uint64_t client_socket, const std::string& status, const std::string& content_type, const std::string& body) {
    std::string response = "HTTP/1.1 " + status + "\r\n";
    response += "Content-Type: " + content_type + "; charset=utf-8\r\n";
    response += "Content-Length: " + std::to_string(body.length()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;

    send(static_cast<SOCKET_TYPE>(client_socket), response.c_str(), static_cast<int>(response.length()), 0);
}

void GuiServer::send_error(uint64_t client_socket, int code, const std::string& message) {
    std::string body = "{\"status\":\"error\",\"error\":\"" + escape_json(message) + "\"}";
    std::string status_str = std::to_string(code) + " ";
    if (code == 400) status_str += "Bad Request";
    else if (code == 404) status_str += "Not Found";
    else if (code == 500) status_str += "Internal Server Error";
    else status_str += "Error";

    send_response(client_socket, status_str, "application/json", body);
}

void GuiServer::handle_api_drives(uint64_t client_socket) {
    std::vector<std::string> drives;
#ifdef _WIN32
    char drive_strings[256];
    DWORD len = GetLogicalDriveStringsA(sizeof(drive_strings), drive_strings);
    if (len > 0 && len < sizeof(drive_strings)) {
        char* drive = drive_strings;
        while (*drive) {
            drives.push_back(drive);
            drive += std::strlen(drive) + 1;
        }
    }
#endif
    if (drives.empty()) {
        drives.push_back("C:\\");
    }

    std::string body = "{\"status\":\"success\",\"drives\":[";
    for (size_t i = 0; i < drives.size(); ++i) {
        body += "\"" + escape_json(drives[i]) + "\"";
        if (i + 1 < drives.size()) {
            body += ",";
        }
    }
    body += "]}";
    send_response(client_socket, "200 OK", "application/json", body);
}

void GuiServer::handle_api_local_list(uint64_t client_socket, const std::string& query) {
    std::string path = get_query_param(query, "path");
    if (path.empty()) {
        send_error(client_socket, 400, "Missing path parameter");
        return;
    }

    try {
        std::vector<file::FileManager::FileInfo> files = file::FileManager::list_directory(path);
        std::string body = "{\"status\":\"success\",\"path\":\"" + escape_json(path) + "\",\"files\":[";

        bool is_root = (path.length() == 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'));
        
        if (!is_root) {
            body += "{";
            body += "\"name\":\"..\",";
            body += "\"path\":\"..\",";
            body += "\"size\":0,";
            body += "\"is_dir\":true,";
            body += "\"last_modified\":0,";
            body += "\"permissions\":0,";
            body += "\"is_symlink\":false,";
            body += "\"symlink_target\":\"\"";
            body += "}";
            if (!files.empty()) {
                body += ",";
            }
        }

        for (size_t i = 0; i < files.size(); ++i) {
            const auto& f = files[i];
            std::string name = file::FileManager::get_filename(f.path);
            body += "{";
            body += "\"name\":\"" + escape_json(name) + "\",";
            body += "\"path\":\"" + escape_json(f.path) + "\",";
            body += "\"size\":" + std::to_string(f.size) + ",";
            body += "\"is_dir\":" + std::string(f.is_directory ? "true" : "false") + ",";
            body += "\"last_modified\":" + std::to_string(f.last_modified) + ",";
            body += "\"permissions\":" + std::to_string(f.permissions) + ",";
            body += "\"is_symlink\":" + std::string(f.is_symlink ? "true" : "false") + ",";
            body += "\"symlink_target\":\"" + escape_json(f.symlink_target) + "\"";
            body += "}";
            if (i + 1 < files.size()) {
                body += ",";
            }
        }
        body += "]}";
        send_response(client_socket, "200 OK", "application/json", body);
    } catch (const std::exception& e) {
        send_error(client_socket, 500, e.what());
    }
}

void GuiServer::handle_api_connect(uint64_t client_socket, const std::string& request_body) {
    std::string host = get_json_value(request_body, "host");
    std::string port_str = get_json_value(request_body, "port");
    std::string username = get_json_value(request_body, "username");
    std::string auth_method = get_json_value(request_body, "auth_method");
    std::string password = get_json_value(request_body, "password");
    std::string private_key_file = get_json_value(request_body, "private_key_file");
    std::string private_key_passphrase = get_json_value(request_body, "private_key_passphrase");
    std::string secret_key = get_json_value(request_body, "secret_key");
    std::string security_level_str = get_json_value(request_body, "security_level");
    std::string allowed_paths_raw = get_json_value(request_body, "allowed_paths");

    if (host.empty() || port_str.empty()) {
        send_error(client_socket, 400, "Host and port are required");
        return;
    }

    uint16_t port = static_cast<uint16_t>(std::stoi(port_str));

    try {
        std::lock_guard<std::mutex> lock(client_mutex_);

        active_client_ = std::make_shared<client::Client>();

        config::ClientConfig conf = config::ClientConfig::get_default();
        conf.auto_create_directories = true;
        conf.create_empty_directories = true;
        if (!secret_key.empty()) {
            conf.secret_key = secret_key;
        }

        if (!username.empty()) {
            conf.username = username;
            conf.auth_method = auth_method;
            conf.password = password;
            conf.private_key_file = private_key_file;
            conf.private_key_passphrase = private_key_passphrase;
        } else {
            conf.auth_method = "none";
        }

        active_client_->set_config(conf);

        crypto::SecurityLevel level = crypto::SecurityLevel::HIGH;
        if (security_level_str == "fast") {
            level = crypto::SecurityLevel::FAST;
        } else if (security_level_str == "aes") {
            level = crypto::SecurityLevel::AES;
        } else if (security_level_str == "AES-256-GCM" || security_level_str == "aes_256_gcm" || security_level_str == "AES_256_GCM") {
            level = crypto::SecurityLevel::AES_256_GCM;
        }

        active_client_->set_security_level(level);
        active_client_->connect(host, port);

        active_client_config_ = conf;
        active_security_level_ = level;

        remote_connected_ = true;
        remote_host_ = host;
        remote_port_ = port;
        remote_username_ = username;

        remote_allowed_paths_.clear();

        if (!allowed_paths_raw.empty()) {
            std::stringstream ss(allowed_paths_raw);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // Trim spaces and quotes
                size_t start = item.find_first_not_of(" \t\r\n\"'");
                size_t end = item.find_last_not_of(" \t\r\n\"'");
                if (start != std::string::npos && end != std::string::npos) {
                    std::string path = item.substr(start, end - start + 1);
                    // Skip "/" to prevent it from being added
                    if (path != "/" && path != "\\") {
                        remote_allowed_paths_.push_back(path);
                    }
                }
            }
        }

        if (remote_allowed_paths_.empty()) {
            // Check local configuration files for possible allowed paths
            std::vector<std::string> search_files = {
                "server.conf",
                "../build_vs/server.conf",
                "D:\\src\\net_copy\\build_vs\\server.conf"
            };
            for (const auto& f : search_files) {
                if (std::filesystem::exists(f)) {
                    try {
                        config::ConfigParser parser;
                        parser.load_from_file(f);
                        auto plist = parser.get_string_list("paths", "allowed_paths");
                        if (!plist.empty()) {
                            for (const auto& p : plist) {
                                remote_allowed_paths_.push_back(p);
                            }
                            break;
                        }
                    } catch (...) {}
                }
            }
        }

        if (remote_allowed_paths_.empty()) {
            remote_allowed_paths_.push_back("D:\\src\\net_copy\\scratch_recv");
            remote_allowed_paths_.push_back("D:\\Work\\FILES");
        }

        std::string body = "{\"success\":true,\"allowed_paths\":[";
        for (size_t i = 0; i < remote_allowed_paths_.size(); ++i) {
            body += "\"" + escape_json(remote_allowed_paths_[i]) + "\"";
            if (i + 1 < remote_allowed_paths_.size()) {
                body += ",";
            }
        }
        body += "]}";

        send_response(client_socket, "200 OK", "application/json", body);
    } catch (const std::exception& e) {
        remote_connected_ = false;
        active_client_.reset();
        send_error(client_socket, 500, std::string("Connection failed: ") + e.what());
    }
}

void GuiServer::handle_api_remote_list(uint64_t client_socket, const std::string& query) {
    std::string path = get_query_param(query, "path");
    if (path.empty()) {
        path = "/";
    }

    std::lock_guard<std::mutex> lock(client_mutex_);
    if (!remote_connected_ || !active_client_) {
        send_error(client_socket, 400, "Not connected to remote server");
        return;
    }

    try {
        auto entries = active_client_->list_remote_directory(path, false);
        std::string body = "{\"status\":\"success\",\"path\":\"" + escape_json(path) + "\",\"files\":[";

        bool is_filesystem_root = (path == "/" || path == "\\" || (path.length() == 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')));
        
        bool is_trusted_root = false;
        std::string normalized_path = path;
        std::replace(normalized_path.begin(), normalized_path.end(), '/', '\\');
        if (!normalized_path.empty() && normalized_path.back() != '\\') {
            normalized_path += '\\';
        }
        
        for (const auto& allowed : remote_allowed_paths_) {
            std::string normalized_allowed = allowed;
            std::replace(normalized_allowed.begin(), normalized_allowed.end(), '/', '\\');
            if (!normalized_allowed.empty() && normalized_allowed.back() != '\\') {
                normalized_allowed += '\\';
            }
            if (normalized_path == normalized_allowed) {
                is_trusted_root = true;
                break;
            }
        }
        
        if (!is_filesystem_root && !is_trusted_root) {
            body += "{";
            body += "\"name\":\"..\",";
            body += "\"path\":\"..\",";
            body += "\"size\":0,";
            body += "\"is_dir\":true,";
            body += "\"last_modified\":0,";
            body += "\"permissions\":0,";
            body += "\"is_symlink\":false,";
            body += "\"symlink_target\":\"\"";
            body += "}";
            if (!entries.empty()) {
                body += ",";
            }
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            std::string name = file::FileManager::get_filename(e.path);
            
            if (name.empty() || name == "\\" || name == "/") {
                name = e.path;
            }
            
            body += "{";
            body += "\"name\":\"" + escape_json(name) + "\",";
            body += "\"path\":\"" + escape_json(e.path) + "\",";
            body += "\"size\":" + std::to_string(e.size) + ",";
            body += "\"is_dir\":" + std::string(e.is_directory ? "true" : "false") + ",";
            body += "\"last_modified\":" + std::to_string(e.last_modified) + ",";
            body += "\"permissions\":" + std::to_string(e.permissions) + ",";
            body += "\"is_symlink\":" + std::string(e.is_symlink ? "true" : "false") + ",";
            body += "\"symlink_target\":\"" + escape_json(e.symlink_target) + "\"";
            body += "}";
            if (i + 1 < entries.size()) {
                body += ",";
            }
        }
        body += "]}";
        send_response(client_socket, "200 OK", "application/json", body);
    } catch (const std::exception& e) {
        send_error(client_socket, 500, e.what());
    }
}

void GuiServer::handle_api_transfer(uint64_t client_socket, const std::string& request_body) {
    std::string direction = get_json_value(request_body, "direction");
    std::string local_path = get_json_value(request_body, "local_path");
    std::string remote_path = get_json_value(request_body, "remote_path");
    std::vector<std::string> items = get_json_array(request_body, "items");
    std::string resume_str = get_json_value(request_body, "resume");
    std::string force_str = get_json_value(request_body, "force");

    bool resume = (resume_str == "true");
    bool force = (force_str == "true");

    if (direction.empty() || local_path.empty() || remote_path.empty() || items.empty()) {
        send_error(client_socket, 400, "Missing required parameters");
        return;
    }

    std::string host;
    uint16_t port = 0;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (!remote_connected_) {
            send_error(client_socket, 400, "Not connected to remote server");
            return;
        }
        host = remote_host_;
        port = remote_port_;
    }

    static std::atomic<uint32_t> g_transfer_id_counter(0);
    std::string transfer_id = "t_" + std::to_string(++g_transfer_id_counter);

    auto transfer = std::make_shared<ActiveTransfer>();
    transfer->id = transfer_id;
    transfer->direction = direction;
    transfer->source = (direction == "upload") ? local_path : remote_path;
    transfer->destination = (direction == "upload") ? remote_path : local_path;
    transfer->active = true;
    transfer->minimized = false;
    transfer->bytes_transferred = 0;
    transfer->total_bytes = 0;
    transfer->current_file = "Initializing...";
    transfer->client_inst = std::make_shared<client::Client>();
    transfer->client_inst->set_config(active_client_config_);
    transfer->client_inst->set_security_level(active_security_level_);

    // Set start time
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream time_ss;
    time_ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    transfer->start_time = time_ss.str();

    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        transfers_[transfer_id] = transfer;
    }

    // Connect separate client instance
    try {
        transfer->client_inst->connect(host, port);
    } catch (const std::exception& e) {
        transfer->active = false;
        transfer->error_message = std::string("Connection failed: ") + e.what();
        send_error(client_socket, 500, transfer->error_message);
        return;
    }

    // Set overwrite callback
    transfer->client_inst->set_overwrite_callback([force](const std::string& /*remote_path*/, uint64_t /*remote_size*/) {
        if (force) {
            return client::Client::OverwriteDecision::OVERWRITE;
        } else {
            return client::Client::OverwriteDecision::CANCEL;
        }
    });

    struct TaskInfo {
        std::string source_path;
        std::string dest_path;
        uint64_t size;
        bool is_directory;
    };

    // Pre-scan local/remote files and build task queue
    std::vector<TaskInfo> tasks;
    try {
        if (direction == "upload") {
            std::filesystem::path p_local_base = std::filesystem::u8path(local_path).lexically_normal();
            std::string norm_local_base = p_local_base.u8string();
            while (!norm_local_base.empty() && (norm_local_base.back() == '/' || norm_local_base.back() == '\\')) {
                norm_local_base.pop_back();
            }
            std::string norm_local_base_cmp = norm_local_base;
            #ifdef _WIN32
            std::transform(norm_local_base_cmp.begin(), norm_local_base_cmp.end(), norm_local_base_cmp.begin(), ::tolower);
            #endif

            for (const auto& item : items) {
                std::string full_local = file::FileManager::join_path(local_path, item);
                if (file::FileManager::is_directory(full_local)) {
                    auto sub = file::FileManager::list_directory(full_local, true);
                    for (const auto& entry : sub) {
                        if (entry.is_directory) continue;
                        
                        std::filesystem::path p_entry = std::filesystem::u8path(entry.path).lexically_normal();
                        std::string norm_entry = p_entry.u8string();
                        std::string norm_entry_cmp = norm_entry;
                        #ifdef _WIN32
                        std::transform(norm_entry_cmp.begin(), norm_entry_cmp.end(), norm_entry_cmp.begin(), ::tolower);
                        #endif
                        
                        std::string relative = entry.path;
                        if (norm_entry_cmp.rfind(norm_local_base_cmp, 0) == 0) {
                            relative = norm_entry.substr(norm_local_base.length());
                        }
                        while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
                            relative.erase(relative.begin());
                        }
                        
                        std::string dest = file::FileManager::join_path(remote_path, common::convert_to_unix_path(relative));
                        tasks.push_back({entry.path, dest, entry.size, false});
                    }
                } else {
                    std::string dest = file::FileManager::join_path(remote_path, common::convert_to_unix_path(item));
                    tasks.push_back({full_local, dest, file::FileManager::file_size(full_local), false});
                }
            }
        } else {
            // download
            client::Client list_client;
            list_client.set_config(active_client_config_);
            list_client.set_security_level(active_security_level_);
            list_client.connect(host, port);

            std::filesystem::path p_remote_base = std::filesystem::u8path(remote_path).lexically_normal();
            std::string norm_remote_base = p_remote_base.u8string();
            while (!norm_remote_base.empty() && (norm_remote_base.back() == '/' || norm_remote_base.back() == '\\')) {
                norm_remote_base.pop_back();
            }
            std::string norm_remote_base_cmp = norm_remote_base;
            #ifdef _WIN32
            std::transform(norm_remote_base_cmp.begin(), norm_remote_base_cmp.end(), norm_remote_base_cmp.begin(), ::tolower);
            #endif

            for (const auto& item : items) {
                std::string full_remote = file::FileManager::join_path(remote_path, item);
                full_remote = common::convert_to_unix_path(full_remote);
                
                bool is_dir = false;
                try {
                    auto parent_list = list_client.list_remote_directory(remote_path, false);
                    for (const auto& entry : parent_list) {
                        std::string entry_name = file::FileManager::get_filename(entry.path);
                        if (entry_name == item || entry.path == item) {
                            is_dir = entry.is_directory;
                            break;
                        }
                    }
                } catch (...) {}
                
                if (is_dir) {
                    auto sub = list_client.list_remote_directory(full_remote, true);
                    for (const auto& entry : sub) {
                        if (entry.is_directory) continue;
                        
                        std::filesystem::path p_entry = std::filesystem::u8path(entry.path).lexically_normal();
                        std::string norm_entry = p_entry.u8string();
                        std::string norm_entry_cmp = norm_entry;
                        #ifdef _WIN32
                        std::transform(norm_entry_cmp.begin(), norm_entry_cmp.end(), norm_entry_cmp.begin(), ::tolower);
                        #endif
                        
                        std::string relative = entry.path;
                        if (norm_entry_cmp.rfind(norm_remote_base_cmp, 0) == 0) {
                            relative = norm_entry.substr(norm_remote_base.length());
                        }
                        while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
                            relative.erase(relative.begin());
                        }
                        
                        std::string dest = file::FileManager::join_path(local_path, relative);
                        tasks.push_back({entry.path, dest, entry.size, false});
                    }
                } else {
                    std::string dest = file::FileManager::join_path(local_path, item);
                    uint64_t size = 0;
                    try {
                        auto parent_list = list_client.list_remote_directory(remote_path, false);
                        for (const auto& entry : parent_list) {
                            std::string entry_name = file::FileManager::get_filename(entry.path);
                            if (entry_name == item || entry.path == item) {
                                size = entry.size;
                                break;
                            }
                        }
                    } catch (...) {}
                    tasks.push_back({full_remote, dest, size, false});
                }
            }
            list_client.disconnect();
        }
    } catch (const std::exception& e) {
        transfer->active = false;
        transfer->error_message = std::string("Listing failed: ") + e.what();
        send_error(client_socket, 500, transfer->error_message);
        return;
    }

    if (tasks.empty()) {
        transfer->active = false;
        transfer->current_file = "Completed";
        send_response(client_socket, "200 OK", "application/json", "{\"success\":true,\"id\":\"" + transfer_id + "\"}");
        return;
    }

    // Perform Conflict Checks and initialize FileProgress array
    std::map<std::string, uint64_t> remote_existing;
    if (direction == "upload") {
        try {
            client::Client list_client;
            list_client.set_config(active_client_config_);
            list_client.set_security_level(active_security_level_);
            list_client.connect(host, port);
            auto existing = list_client.list_remote_directory(remote_path, true);
            for (const auto& f : existing) {
                if (!f.is_directory) {
                    std::string path_norm = f.path;
                    std::replace(path_norm.begin(), path_norm.end(), '\\', '/');
                    #ifdef _WIN32
                    std::transform(path_norm.begin(), path_norm.end(), path_norm.begin(), ::tolower);
                    #endif
                    remote_existing[path_norm] = f.size;
                }
            }
            list_client.disconnect();
        } catch (...) {}
    }

    uint64_t total_transfer_bytes = 0;
    {
        std::lock_guard<std::mutex> t_lock(transfer->mutex);
        for (const auto& task : tasks) {
            FileProgress fp;
            fp.path = task.source_path; // display source path
            fp.total_bytes = task.size;
            fp.bytes_transferred = 0;
            fp.is_directory = false;
            fp.status = "pending";
            fp.decision = "none";

            bool exists = false;
            uint64_t dest_size = 0;

            if (direction == "upload") {
                std::string lookup = task.dest_path;
                std::replace(lookup.begin(), lookup.end(), '\\', '/');
                #ifdef _WIN32
                std::transform(lookup.begin(), lookup.end(), lookup.begin(), ::tolower);
                #endif
                auto it = remote_existing.find(lookup);
                if (it != remote_existing.end()) {
                    exists = true;
                    dest_size = it->second;
                }
            } else {
                std::filesystem::path p_dest(std::filesystem::u8path(task.dest_path));
                if (std::filesystem::exists(p_dest)) {
                    exists = true;
                    dest_size = std::filesystem::file_size(p_dest);
                }
            }

            if (exists) {
                if (dest_size == task.size) {
                    fp.status = "exists_exact";
                    fp.decision = "none";
                    fp.bytes_transferred = task.size;
                } else if (dest_size < task.size) {
                    fp.status = "exists_partial";
                    fp.decision = "none";
                    fp.bytes_transferred = dest_size;
                } else {
                    fp.status = "exists_exact";
                    fp.decision = "none";
                    fp.bytes_transferred = task.size;
                }
            } else {
                fp.status = "pending";
                fp.decision = "start"; // new files can start automatically
            }

            transfer->files.push_back(fp);
            total_transfer_bytes += fp.total_bytes;
        }
        transfer->total_bytes = total_transfer_bytes;
        transfer->current_file = "Waiting for decision...";
    }

    // Set progress callback to update individual file stats
    struct ProgressTracker {
        std::mutex mutex;
        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
    };
    auto tracker = std::make_shared<ProgressTracker>();

    auto progress_cb = [transfer, tracker](uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file) {
        std::lock_guard<std::mutex> lock(transfer->mutex);
        
        bool found = false;
        for (auto& f : transfer->files) {
            auto norm_match = [](const std::string& p1, const std::string& p2) {
                std::string n1 = p1; std::string n2 = p2;
                std::replace(n1.begin(), n1.end(), '\\', '/');
                std::replace(n2.begin(), n2.end(), '\\', '/');
                #ifdef _WIN32
                std::transform(n1.begin(), n1.end(), n1.begin(), ::tolower);
                std::transform(n2.begin(), n2.end(), n2.begin(), ::tolower);
                #endif
                if (n1.length() >= n2.length() && n1.compare(n1.length() - n2.length(), n2.length(), n2) == 0) return true;
                if (n2.length() >= n1.length() && n2.compare(n2.length() - n1.length(), n1.length(), n1) == 0) return true;
                return false;
            };

            if (norm_match(f.path, current_file)) {
                auto now = std::chrono::steady_clock::now();
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - f.last_time).count();
                if (duration_ms >= 500) {
                    double speed = static_cast<double>(bytes_transferred - f.last_bytes) / (static_cast<double>(duration_ms) / 1000.0);
                    f.rate_string = format_rate(speed);
                    f.last_bytes = bytes_transferred;
                    f.last_time = now;
                } else if (f.rate_string == "0 B/s" || f.rate_string.empty()) {
                    if (bytes_transferred > 0 && duration_ms > 0) {
                        double speed = static_cast<double>(bytes_transferred) / (static_cast<double>(duration_ms) / 1000.0);
                        f.rate_string = format_rate(speed);
                    }
                }

                f.bytes_transferred = bytes_transferred;
                if (bytes_transferred >= total_bytes && total_bytes > 0) {
                    f.status = "completed";
                    f.rate_string = "0 B/s";
                } else if (transfer->client_inst->is_file_paused(f.path)) {
                    f.status = "paused";
                    f.rate_string = "0 B/s";
                } else if (transfer->client_inst->is_file_skipped(f.path)) {
                    f.status = "skipped";
                    f.rate_string = "0 B/s";
                } else {
                    f.status = "transferring";
                }
                found = true;
                break;
            }
        }

        if (!found) {
            FileProgress fp;
            fp.path = current_file;
            fp.total_bytes = total_bytes;
            fp.bytes_transferred = bytes_transferred;
            fp.last_bytes = bytes_transferred;
            fp.last_time = std::chrono::steady_clock::now();
            if (bytes_transferred >= total_bytes && total_bytes > 0) {
                fp.status = "completed";
                fp.rate_string = "0 B/s";
            } else if (transfer->client_inst->is_file_paused(current_file)) {
                fp.status = "paused";
                fp.rate_string = "0 B/s";
            } else if (transfer->client_inst->is_file_skipped(current_file)) {
                fp.status = "skipped";
                fp.rate_string = "0 B/s";
            } else {
                fp.status = "transferring";
                fp.rate_string = "0 B/s";
            }
            transfer->files.push_back(fp);
        }

        uint64_t total_done = 0;
        uint64_t total_size = 0;
        for (const auto& f : transfer->files) {
            total_done += f.bytes_transferred;
            total_size += f.total_bytes;
        }
        transfer->bytes_transferred = total_done;
        if (total_size > 0) {
            transfer->total_bytes = total_size;
        } else {
            transfer->total_bytes = total_bytes;
        }

        transfer->current_file = file::FileManager::get_filename(current_file);
    };

    transfer->client_inst->set_progress_callback(progress_cb);

    transfer->thread = std::thread([this, transfer, direction, local_path, remote_path, items, resume, host, port, progress_cb, force, tasks]() {
        try {
            std::exception_ptr first_error;
            std::mutex error_mutex;

            auto record_error = [&](std::exception_ptr error) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!first_error) {
                    first_error = error;
                }
            };

            uint32_t max_threads = transfer->client_inst->get_negotiated_parallel_streams();
            if (max_threads == 0) max_threads = 4;
            
            bool single_large_file = (tasks.size() == 1 && tasks[0].size > 64 * 1024 * 1024);
            uint32_t num_workers = max_threads;
            if (single_large_file) {
                num_workers = 1;
            } else {
                num_workers = (std::min)(max_threads, static_cast<uint32_t>(tasks.size()));
            }

            std::vector<std::thread> worker_threads;
            for (uint32_t w = 0; w < num_workers; ++w) {
                worker_threads.push_back(std::thread([this, transfer, direction, host, port, progress_cb, force, &first_error, &record_error, tasks, single_large_file, max_threads]() {
                    try {
                        client::Client stream_client;
                        stream_client.set_config(active_client_config_);
                        stream_client.set_security_level(active_security_level_);
                        
                        uint32_t streams = single_large_file ? max_threads : 1;
                        stream_client.set_requested_parallel_streams(streams);
                        
                        stream_client.bandwidth_limiter_ = transfer->client_inst->bandwidth_limiter_;
                        stream_client.set_progress_callback(progress_cb);
                        
                        // Overwrite callback is set dynamically per task below

                        stream_client.set_parent_client(transfer->client_inst.get());
                        transfer->client_inst->register_worker(&stream_client);

                        while (true) {
                            if (transfer->client_inst->cancel_requested_ || stream_client.cancel_requested_ || transfer->cancelled || first_error) {
                                break;
                            }
                            
                            if (!stream_client.is_connected()) {
                                try {
                                    stream_client.connect(host, port);
                                } catch (...) {
                                    // Connection failed. Wait 2 seconds on cv_task and retry.
                                    std::unique_lock<std::mutex> lock(transfer->task_mutex);
                                    transfer->cv_task.wait_for(lock, std::chrono::seconds(2));
                                    continue;
                                }
                            }
                            
                            size_t task_idx = -1;
                            std::string task_decision;
                            
                            {
                                std::unique_lock<std::mutex> lock(transfer->task_mutex);
                                while (true) {
                                    if (transfer->client_inst->cancel_requested_ || stream_client.cancel_requested_ || transfer->cancelled || first_error) {
                                        break;
                                    }

                                    bool all_completed_or_skipped = true;
                                    for (size_t i = 0; i < tasks.size(); ++i) {
                                        auto& f = transfer->files[i];
                                        if (f.status != "completed" && f.status != "skipped" && f.status != "failed") {
                                            all_completed_or_skipped = false;
                                        }
                                        if (f.decision != "none" && f.status != "transferring" && f.status != "paused" && f.status != "completed" && f.status != "skipped" && f.status != "failed") {
                                            task_idx = i;
                                            task_decision = f.decision;
                                            f.status = "transferring";
                                            f.popped = true;
                                            break;
                                        }
                                    }

                                    if (task_idx != -1 || all_completed_or_skipped) {
                                        break;
                                    }

                                    transfer->cv_task.wait_for(lock, std::chrono::milliseconds(500));
                                }
                            }

                            if (task_idx == -1) {
                                break;
                            }

                            const auto& task = tasks[task_idx];
                            bool resume_file = (task_decision == "resume");
                            
                            stream_client.set_overwrite_callback([task_decision](const std::string&, uint64_t) {
                                if (task_decision == "delta_sync") {
                                    return client::Client::OverwriteDecision::DELTA_SYNC;
                                }
                                return client::Client::OverwriteDecision::OVERWRITE;
                            });

                            try {
                                if (direction == "upload") {
                                    stream_client.transfer_file(task.source_path, task.dest_path, resume_file);
                                } else {
                                    std::string dest_dir = file::FileManager::get_directory(task.dest_path);
                                    if (!dest_dir.empty()) {
                                        file::FileManager::create_directories(dest_dir);
                                    }
                                    stream_client.download_file(task.source_path, task.dest_path, resume_file);
                                }

                                if (!stream_client.get_session_id().empty()) {
                                    std::lock_guard<std::mutex> t_lock(transfer->mutex);
                                    if (std::find(transfer->session_ids.begin(), transfer->session_ids.end(), stream_client.get_session_id()) == transfer->session_ids.end()) {
                                        transfer->session_ids.push_back(stream_client.get_session_id());
                                    }
                                    if (transfer->session_id.empty()) {
                                        transfer->session_id = stream_client.get_session_id();
                                    }
                                }

                                {
                                    std::lock_guard<std::mutex> t_lock(transfer->mutex);
                                    transfer->files[task_idx].status = "completed";
                                    transfer->files[task_idx].bytes_transferred = task.size;
                                    transfer->files[task_idx].rate_string = "0 B/s";
                                    transfer->files[task_idx].popped = false;
                                }
                            } catch (const FileSkippedException&) {
                                std::lock_guard<std::mutex> t_lock(transfer->mutex);
                                transfer->files[task_idx].status = "skipped";
                                transfer->files[task_idx].rate_string = "0 B/s";
                                transfer->files[task_idx].popped = false;
                                stream_client.disconnect();
                            } catch (...) {
                                bool server_down = false;
                                {
                                    std::lock_guard<std::mutex> lock(client_mutex_);
                                    if (!remote_connected_) {
                                        server_down = true;
                                    }
                                }
                                if (!server_down) {
                                    if (!stream_client.is_connected()) {
                                        server_down = true;
                                    }
                                }

                                if (server_down) {
                                    {
                                        std::lock_guard<std::mutex> t_lock(transfer->mutex);
                                        transfer->files[task_idx].status = "paused";
                                        transfer->files[task_idx].decision = "resume";
                                        transfer->files[task_idx].rate_string = "0 B/s";
                                        transfer->files[task_idx].popped = false;
                                    }
                                    try { stream_client.disconnect(); } catch (...) {}
                                    LOG_INFO("Worker detected server disconnection, pausing file: " + task.source_path);
                                    record_error(std::current_exception());
                                    break;
                                } else {
                                    {
                                        std::lock_guard<std::mutex> t_lock(transfer->mutex);
                                        transfer->files[task_idx].status = "failed";
                                        transfer->files[task_idx].rate_string = "0 B/s";
                                        transfer->files[task_idx].popped = false;
                                    }
                                    // Log the error but continue with the next file
                                    try {
                                        std::rethrow_exception(std::current_exception());
                                    } catch (const std::exception& e) {
                                        LOG_ERROR("File transfer failed for " + task.source_path + ": " + e.what());
                                    }
                                    continue;
                                }
                            }
                        }

                        transfer->client_inst->unregister_worker(&stream_client);
                    } catch (...) {
                        record_error(std::current_exception());
                    }
                }));
            }

            for (auto& t : worker_threads) {
                if (t.joinable()) t.join();
            }

            if (first_error) {
                std::rethrow_exception(first_error);
            }

            // Mark completed
            {
                std::lock_guard<std::mutex> lock(transfer->mutex);
                transfer->bytes_transferred = transfer->total_bytes.load();
                transfer->current_file = "Completed";
                for (auto& f : transfer->files) {
                    if (f.status == "transferring" || f.status == "pending") {
                        f.status = "completed";
                        f.bytes_transferred = f.total_bytes;
                    }
                }
            }
            transfer->active = false;
        } catch (const std::exception& e) {
            transfer->error_message = e.what();
            transfer->active = false;
            std::lock_guard<std::mutex> lock(transfer->mutex);
            for (auto& f : transfer->files) {
                if (f.status == "transferring" || f.status == "pending") {
                    f.status = "paused";
                }
            }
        }
        
        try {
            transfer->client_inst->disconnect();
        } catch (...) {}
    });

    transfer->thread.detach();

    std::string resp_body = "{\"success\":true,\"id\":\"" + transfer_id + "\"}";
    send_response(client_socket, "200 OK", "application/json", resp_body);
}

void GuiServer::handle_api_transfers(uint64_t client_socket) {
    std::lock_guard<std::mutex> lock(transfers_mutex_);
    std::string body = "[";
    size_t count = 0;
    for (const auto& pair : transfers_) {
        const auto& t = pair.second;
        std::lock_guard<std::mutex> t_lock(t->mutex);
        
        double percent = 0.0;
        if (t->total_bytes > 0) {
            percent = (static_cast<double>(t->bytes_transferred.load()) / static_cast<double>(t->total_bytes.load())) * 100.0;
            if (percent > 100.0) percent = 100.0;
        }
        
        body += "{";
        body += "\"id\":\"" + escape_json(t->id) + "\",";
        body += "\"direction\":\"" + escape_json(t->direction) + "\",";
        body += "\"source\":\"" + escape_json(t->source) + "\",";
        body += "\"destination\":\"" + escape_json(t->destination) + "\",";
        body += "\"active\":" + std::string(t->active.load() ? "true" : "false") + ",";
        body += "\"minimized\":" + std::string(t->minimized.load() ? "true" : "false") + ",";
        body += "\"bytes_transferred\":" + std::to_string(t->bytes_transferred.load()) + ",";
        body += "\"total_bytes\":" + std::to_string(t->total_bytes.load()) + ",";
        body += "\"percent\":" + std::to_string(percent) + ",";
        body += "\"current_file\":\"" + escape_json(t->current_file) + "\",";
        body += "\"start_time\":\"" + escape_json(t->start_time) + "\",";
        body += "\"session_id\":\"" + escape_json(t->session_id) + "\",";
        body += "\"error\":\"" + escape_json(t->error_message) + "\"";
        body += "}";
        
        if (++count < transfers_.size()) {
            body += ",";
        }
    }
    body += "]";
    send_response(client_socket, "200 OK", "application/json", body);
}

void GuiServer::handle_api_transfer_status(uint64_t client_socket, const std::string& query) {
    std::string id = get_query_param(query, "id");
    if (id.empty()) {
        bool active = transfer_active_;
        uint64_t bytes = transfer_bytes_;
        uint64_t total = transfer_total_bytes_;
        double rate = transfer_rate_;
        std::string rate_str = transfer_rate_string_;
        std::string current_file = transfer_current_file_;
        std::string error = transfer_error_;

        double percent = 0.0;
        if (total > 0) {
            percent = (static_cast<double>(bytes) / static_cast<double>(total)) * 100.0;
            if (percent > 100.0) percent = 100.0;
        }

        std::string body = "{";
        body += "\"active\":" + std::string(active ? "true" : "false") + ",";
        body += "\"current_file\":\"" + escape_json(current_file) + "\",";
        body += "\"bytes_transferred\":" + std::to_string(bytes) + ",";
        body += "\"total_bytes\":" + std::to_string(total) + ",";
        body += "\"percent\":" + std::to_string(percent) + ",";
        body += "\"rate_string\":\"" + escape_json(rate_str) + "\",";
        body += "\"error\":\"" + escape_json(error) + "\",";
        body += "\"files\":[]";
        body += "}";

        send_response(client_socket, "200 OK", "application/json", body);
        return;
    }

    std::shared_ptr<ActiveTransfer> t;
    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            t = it->second;
        }
    }

    if (!t) {
        send_error(client_socket, 404, "Transfer not found");
        return;
    }

    std::lock_guard<std::mutex> t_lock(t->mutex);
    double percent = 0.0;
    if (t->total_bytes > 0) {
        percent = (static_cast<double>(t->bytes_transferred.load()) / static_cast<double>(t->total_bytes.load())) * 100.0;
        if (percent > 100.0) percent = 100.0;
    }

    std::string body = "{";
    body += "\"id\":\"" + escape_json(t->id) + "\",";
    body += "\"direction\":\"" + escape_json(t->direction) + "\",";
    body += "\"source\":\"" + escape_json(t->source) + "\",";
    body += "\"destination\":\"" + escape_json(t->destination) + "\",";
    body += "\"active\":" + std::string(t->active.load() ? "true" : "false") + ",";
    body += "\"minimized\":" + std::string(t->minimized.load() ? "true" : "false") + ",";
    body += "\"bytes_transferred\":" + std::to_string(t->bytes_transferred.load()) + ",";
    body += "\"total_bytes\":" + std::to_string(t->total_bytes.load()) + ",";
    body += "\"percent\":" + std::to_string(percent) + ",";
    body += "\"current_file\":\"" + escape_json(t->current_file) + "\",";
    body += "\"error\":\"" + escape_json(t->error_message) + "\",";
    body += "\"files\":[";
    for (size_t i = 0; i < t->files.size(); ++i) {
        const auto& f = t->files[i];
        
        std::string file_status = f.status;
        if (t->client_inst) {
            if (t->client_inst->is_file_skipped(f.path)) {
                file_status = "skipped";
            } else if (t->client_inst->is_file_paused(f.path) && file_status == "transferring") {
                file_status = "paused";
            }
        }

        body += "{";
        body += "\"path\":\"" + escape_json(f.path) + "\",";
        body += "\"bytes_transferred\":" + std::to_string(f.bytes_transferred) + ",";
        body += "\"total_bytes\":" + std::to_string(f.total_bytes) + ",";
        body += "\"status\":\"" + escape_json(file_status) + "\",";
        body += "\"rate_string\":\"" + escape_json(f.rate_string) + "\",";
        body += "\"decision\":\"" + escape_json(f.decision) + "\"";
        body += "}";
        if (i + 1 < t->files.size()) {
            body += ",";
        }
    }
    body += "]}";

    send_response(client_socket, "200 OK", "application/json", body);
}

void GuiServer::handle_api_transfer_abort(uint64_t client_socket, const std::string& request_body) {
    std::string id = get_json_value(request_body, "id");
    if (id.empty()) {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (active_client_) {
            active_client_->request_cancel();
        }
        transfer_active_ = false;
        transfer_error_ = "Transfer aborted by user";
        send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
        return;
    }

    std::shared_ptr<ActiveTransfer> t;
    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            t = it->second;
        }
    }

    if (!t) {
        send_error(client_socket, 404, "Transfer not found");
        return;
    }

    if (t->client_inst) {
        t->client_inst->request_cancel();
    }
    t->active = false;
    t->error_message = "Transfer aborted by user";
    send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
}

void GuiServer::handle_api_transfer_minimize(uint64_t client_socket, const std::string& request_body) {
    std::string id = get_json_value(request_body, "id");
    std::string minimized_str = get_json_value(request_body, "minimized");
    bool minimized = (minimized_str == "true");

    if (id.empty()) {
        send_error(client_socket, 400, "Missing transfer ID");
        return;
    }

    std::shared_ptr<ActiveTransfer> t;
    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            t = it->second;
        }
    }

    if (!t) {
        send_error(client_socket, 404, "Transfer not found");
        return;
    }

    t->minimized = minimized;
    send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
}

void GuiServer::handle_api_transfer_file_control(uint64_t client_socket, const std::string& request_body) {
    std::string id = get_json_value(request_body, "id");
    std::string path = get_json_value(request_body, "path");
    std::string action = get_json_value(request_body, "action"); // "pause", "resume", "skip", "start", "overwrite", "re-transfer"

    if (id.empty() || path.empty() || action.empty()) {
        send_error(client_socket, 400, "Missing required parameters");
        return;
    }

    std::shared_ptr<ActiveTransfer> t;
    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            t = it->second;
        }
    }

    if (!t) {
        send_error(client_socket, 404, "Transfer not found");
        return;
    }

    if (!t->client_inst) {
        send_error(client_socket, 400, "Transfer client not initialized");
        return;
    }

    std::lock_guard<std::mutex> t_lock(t->mutex);
    bool found = false;
    std::string path_norm = normalize_gui_path(path);
    for (auto& f : t->files) {
        if (normalize_gui_path(f.path) == path_norm) {
            if (action == "pause") {
                t->client_inst->pause_file(f.path);
                f.status = "paused";
            } else if (action == "resume" || action == "start" || action == "overwrite" || action == "re-transfer" || action == "delta_sync") {
                if (f.status == "paused") {
                    t->client_inst->resume_file(f.path);
                    if (f.popped) {
                        f.status = "transferring";
                    } else {
                        f.decision = "resume";
                        f.status = "pending";
                    }
                    t->cv_task.notify_all();
                } else {
                    f.decision = action;
                    f.status = "pending";
                    if (action == "overwrite" || action == "re-transfer") {
                        f.bytes_transferred = 0;
                    }
                    t->cv_task.notify_all();
                }
            } else if (action == "skip") {
                t->client_inst->skip_file(f.path);
                f.status = "skipped";
            }
            found = true;
            break;
        }
    }

    if (!found) {
        if (action == "pause") {
            t->client_inst->pause_file(path);
        } else if (action == "resume" || action == "start" || action == "overwrite" || action == "re-transfer" || action == "delta_sync") {
            t->client_inst->resume_file(path);
        } else if (action == "skip") {
            t->client_inst->skip_file(path);
        }
    }

    send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
}

std::string GuiServer::url_decode(const std::string& src) {
    std::string ret;
    char ch;
    int ii;
    for (size_t i = 0; i < src.length(); i++) {
        if (src[i] == '%') {
            if (i + 2 < src.length() && std::sscanf(src.substr(i + 1, 2).c_str(), "%x", &ii) == 1) {
                ch = static_cast<char>(ii);
                ret += ch;
                i += 2;
            }
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

std::string GuiServer::get_query_param(const std::string& query, const std::string& param) {
    std::string search = param + "=";
    size_t pos = query.find(search);
    if (pos == std::string::npos) {
        search = "&" + param + "=";
        pos = query.find(search);
        if (pos == std::string::npos) {
            return "";
        }
    }
    size_t val_start = pos + search.length();
    size_t val_end = query.find('&', val_start);
    if (val_end == std::string::npos) {
        return url_decode(query.substr(val_start));
    }
    return url_decode(query.substr(val_start, val_end - val_start));
}

std::string GuiServer::get_json_value(const std::string& json, const std::string& key) {
    std::string quoted_key = "\"" + key + "\"";
    size_t pos = json.find(quoted_key);
    if (pos == std::string::npos) {
        return "";
    }
    pos = json.find(':', pos + quoted_key.length());
    if (pos == std::string::npos) {
        return "";
    }
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.length()) return "";

    if (json[pos] == '"') {
        pos++;
        std::string val;
        while (pos < json.length() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.length()) {
                pos++;
                if (json[pos] == 'n') val += '\n';
                else if (json[pos] == 'r') val += '\r';
                else if (json[pos] == 't') val += '\t';
                else val += json[pos];
            } else {
                val += json[pos];
            }
            pos++;
        }
        return val;
    } else {
        size_t end = pos;
        while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
            end++;
        }
        std::string val = json.substr(pos, end - pos);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r' || val.back() == '\n')) {
            val.pop_back();
        }
        return val;
    }
}

std::vector<std::string> GuiServer::get_json_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string quoted_key = "\"" + key + "\"";
    size_t pos = json.find(quoted_key);
    if (pos == std::string::npos) {
        return result;
    }
    pos = json.find(':', pos + quoted_key.length());
    if (pos == std::string::npos) {
        return result;
    }
    pos = json.find('[', pos);
    if (pos == std::string::npos) {
        return result;
    }
    pos++;

    while (pos < json.length()) {
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n' || json[pos] == ',')) {
            pos++;
        }
        if (pos >= json.length() || json[pos] == ']') {
            break;
        }
        if (json[pos] == '"') {
            pos++;
            std::string val;
            while (pos < json.length() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.length()) {
                    pos++;
                    val += json[pos];
                } else {
                    val += json[pos];
                }
                pos++;
            }
            result.push_back(val);
            if (pos < json.length()) pos++;
        } else {
            pos++;
        }
    }
    return result;
}

void GuiServer::handle_api_transfer_remove(uint64_t client_socket, const std::string& request_body) {
    std::string id = get_json_value(request_body, "id");
    if (id.empty()) {
        send_error(client_socket, 400, "Missing transfer ID");
        return;
    }

    std::lock_guard<std::mutex> lock(transfers_mutex_);
    auto it = transfers_.find(id);
    if (it != transfers_.end()) {
        auto t = it->second;
        if (t->active) {
            if (t->client_inst) {
                t->client_inst->request_cancel();
            }
            t->cancelled = true;
            t->active = false;
            t->cv_task.notify_all();
        }
        if (t->thread.joinable()) {
            t->thread.detach();
        }
    }
    send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
}

void GuiServer::handle_api_transfer_control(uint64_t client_socket, const std::string& request_body) {
    std::string id = get_json_value(request_body, "id");
    std::string action = get_json_value(request_body, "action"); // "pause", "resume", "overwrite_all", "overwrite_resume_all", "start_all"

    if (id.empty() || action.empty()) {
        send_error(client_socket, 400, "Missing required parameters");
        return;
    }

    std::shared_ptr<ActiveTransfer> t;
    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            t = it->second;
        }
    }

    if (!t) {
        send_error(client_socket, 404, "Transfer not found");
        return;
    }

    if (!t->client_inst) {
        send_error(client_socket, 400, "Transfer client not initialized");
        return;
    }

    std::lock_guard<std::mutex> t_lock(t->mutex);
    for (auto& f : t->files) {
        if (action == "pause") {
            t->client_inst->pause_file(f.path);
            if (f.status == "transferring" || f.status == "pending") {
                f.status = "paused";
            }
        } else if (action == "resume" || action == "start_all") {
            if (f.status == "paused") {
                t->client_inst->resume_file(f.path);
                if (f.popped) {
                    f.status = "transferring";
                } else {
                    f.decision = "resume";
                    f.status = "pending";
                }
            } else if (f.status == "exists_exact") {
                f.decision = "overwrite";
                f.status = "pending";
                f.bytes_transferred = 0;
            } else if (f.status == "exists_partial") {
                f.decision = "resume";
                f.status = "pending";
            } else if (f.status == "pending" && f.decision == "none") {
                f.decision = "start";
                f.status = "pending";
            }
        } else if (action == "overwrite_all") {
            if (f.status == "exists_exact" || f.status == "exists_partial") {
                f.decision = "overwrite";
                f.status = "pending";
                f.bytes_transferred = 0;
            } else if (f.status == "pending" && f.decision == "none") {
                f.decision = "start";
                f.status = "pending";
            }
        } else if (action == "overwrite_resume_all") {
            if (f.status == "exists_exact") {
                f.decision = "overwrite";
                f.status = "pending";
                f.bytes_transferred = 0;
            } else if (f.status == "exists_partial") {
                f.decision = "resume";
                f.status = "pending";
            } else if (f.status == "pending" && f.decision == "none") {
                f.decision = "start";
                f.status = "pending";
            }
        } else if (action == "delta_sync_all") {
            if (f.status == "exists_exact" || f.status == "exists_partial") {
                f.decision = "delta_sync";
                f.status = "pending";
                f.bytes_transferred = 0;
            } else if (f.status == "pending" && f.decision == "none") {
                f.decision = "start";
                f.status = "pending";
            }
        } else if (action == "delta_sync_resume_all") {
            if (f.status == "exists_exact") {
                f.decision = "delta_sync";
                f.status = "pending";
                f.bytes_transferred = 0;
            } else if (f.status == "exists_partial") {
                f.decision = "resume";
                f.status = "pending";
            } else if (f.status == "pending" && f.decision == "none") {
                f.decision = "start";
                f.status = "pending";
            }
        }
    }
    t->cv_task.notify_all();

    send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
}

void GuiServer::handle_api_transfer_server_session(uint64_t client_socket, const std::string& query) {
    std::string id = get_query_param(query, "id");
    if (id.empty()) {
        send_error(client_socket, 400, "Missing transfer ID");
        return;
    }

    std::shared_ptr<ActiveTransfer> t;
    {
        std::lock_guard<std::mutex> lock(transfers_mutex_);
        auto it = transfers_.find(id);
        if (it != transfers_.end()) {
            t = it->second;
        }
    }

    if (!t) {
        send_error(client_socket, 404, "Transfer not found");
        return;
    }

    std::vector<std::string> session_ids;
    {
        std::lock_guard<std::mutex> t_lock(t->mutex);
        session_ids = t->session_ids;
        if (session_ids.empty() && !t->session_id.empty()) {
            session_ids.push_back(t->session_id);
        }
    }

    if (session_ids.empty()) {
        send_response(client_socket, "200 OK", "application/json", 
            "{\"success\":false,\"error\":\"No server session ID generated yet.\"}");
        return;
    }

    try {
        client::Client temp_client;
        temp_client.set_config(active_client_config_);
        temp_client.set_security_level(active_security_level_);
        temp_client.connect(remote_host_, remote_port_);
        
        uint64_t total_bytes_transferred = 0;
        uint64_t total_bytes_limit = 0;
        bool any_active = false;
        std::string aggregated_logs;
        std::string final_status = "completed";

        for (const auto& sess_id : session_ids) {
            auto resp = temp_client.query_transfer_status(sess_id);
            if (resp.success) {
                total_bytes_transferred += resp.bytes_transferred;
                total_bytes_limit += resp.total_bytes;
                if (resp.active) {
                    any_active = true;
                    final_status = "active";
                }
                if (!resp.active && resp.status_string == "failed") {
                    if (final_status != "active") {
                        final_status = "failed";
                    }
                }
                if (!resp.logs.empty()) {
                    aggregated_logs += "[Session " + sess_id + "]\n" + resp.logs + "\n";
                }
            }
        }
        temp_client.disconnect();

        uint64_t local_transferred = 0;
        uint64_t local_total = 0;
        std::string direction;
        std::string source;
        std::string destination;
        std::string files_json = "[";
        
        {
            std::lock_guard<std::mutex> t_lock(t->mutex);
            local_transferred = t->bytes_transferred;
            local_total = t->total_bytes;
            direction = t->direction;
            source = t->source;
            destination = t->destination;
            for (size_t i = 0; i < t->files.size(); ++i) {
                const auto& f = t->files[i];
                files_json += "{";
                files_json += "\"path\":\"" + escape_json(f.path) + "\",";
                files_json += "\"total_bytes\":" + std::to_string(f.total_bytes) + ",";
                files_json += "\"bytes_transferred\":" + std::to_string(f.bytes_transferred) + ",";
                files_json += "\"status\":\"" + escape_json(f.status) + "\"";
                files_json += "}";
                if (i + 1 < t->files.size()) {
                    files_json += ",";
                }
            }
        }
        files_json += "]";

        std::string body = "{";
        body += "\"success\":true,";
        body += "\"session_id\":\"" + escape_json(session_ids[0]) + "\",";
        body += "\"status_string\":\"" + escape_json(final_status) + "\",";
        body += "\"bytes_transferred\":" + std::to_string(local_transferred) + ",";
        body += "\"total_bytes\":" + std::to_string(local_total) + ",";
        body += "\"direction\":\"" + escape_json(direction) + "\",";
        body += "\"source\":\"" + escape_json(source) + "\",";
        body += "\"destination\":\"" + escape_json(destination) + "\",";
        body += "\"files\":" + files_json + ",";
        body += "\"active\":" + std::string(any_active ? "true" : "false") + ",";
        body += "\"logs\":\"" + escape_json(aggregated_logs) + "\"";
        body += "}";
        send_response(client_socket, "200 OK", "application/json", body);

    } catch (const std::exception& e) {
        send_response(client_socket, "200 OK", "application/json", 
            "{\"success\":false,\"error\":\"Query failed: " + escape_json(e.what()) + "\"}");
    }
}

void GuiServer::handle_api_remote_check(uint64_t client_socket) {
    bool connected = false;
    std::string host;
    uint16_t port = 0;
    
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        connected = remote_connected_;
        host = remote_host_;
        port = remote_port_;
    }

    if (connected) {
        try {
            client::Client check_client;
            check_client.set_config(active_client_config_);
            check_client.set_security_level(active_security_level_);
            check_client.connect(host, port);
            check_client.disconnect();
        } catch (...) {
            connected = false;
        }
    }

    if (!connected) {
        std::lock_guard<std::mutex> lock(client_mutex_);
        remote_connected_ = false;
        active_client_ = nullptr;
    }

    std::string body = "{\"connected\":" + std::string(connected ? "true" : "false") + "}";
    send_response(client_socket, "200 OK", "application/json", body);
}

void GuiServer::handle_api_disconnect(uint64_t client_socket) {
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        remote_connected_ = false;
        if (active_client_) {
            try {
                active_client_->disconnect();
            } catch (...) {}
            active_client_ = nullptr;
        }
    }
    send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
}

void GuiServer::handle_api_local_create_dir(uint64_t client_socket, const std::string& query) {
    std::string parent = get_query_param(query, "path");
    std::string name = get_query_param(query, "name");
    
    parent = url_decode(parent);
    name = url_decode(name);

    if (parent.empty() || name.empty()) {
        send_error(client_socket, 400, "Path and name are required");
        return;
    }

    std::string full_path = file::FileManager::join_path(parent, name);
    try {
        if (file::FileManager::create_directories(full_path)) {
            send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
        } else {
            send_response(client_socket, "200 OK", "application/json", "{\"success\":false,\"error\":\"Folder already exists or creation failed\"}");
        }
    } catch (const std::exception& e) {
        send_response(client_socket, "200 OK", "application/json", "{\"success\":false,\"error\":\"" + escape_json(e.what()) + "\"}");
    }
}

void GuiServer::handle_api_remote_create_dir(uint64_t client_socket, const std::string& query) {
    std::string parent = get_query_param(query, "path");
    std::string name = get_query_param(query, "name");
    
    parent = url_decode(parent);
    name = url_decode(name);

    if (parent.empty() || name.empty()) {
        send_error(client_socket, 400, "Path and name are required");
        return;
    }

    std::string full_path = file::FileManager::join_path(parent, name);
    full_path = common::convert_to_unix_path(full_path);

    try {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (!remote_connected_ || !active_client_) {
            send_error(client_socket, 400, "Not connected to remote server");
            return;
        }
        active_client_->create_empty_directory(full_path);
        send_response(client_socket, "200 OK", "application/json", "{\"success\":true}");
    } catch (const std::exception& e) {
        send_response(client_socket, "200 OK", "application/json", "{\"success\":false,\"error\":\"" + escape_json(e.what()) + "\"}");
    }
}

} // namespace gui
} // namespace netcopy
