#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include "client/client.h"
#include "network/event_loop.h"
#include <asio.hpp>

#include <unordered_map>
#include <chrono>

namespace netcopy {
namespace gui {

struct FileProgress {
    std::string path;
    uint64_t bytes_transferred = 0;
    uint64_t total_bytes = 0;
    std::string status = "pending"; // "pending", "transferring", "paused", "skipped", "completed", "failed", "exists_exact", "exists_partial"
    std::string rate_string = "0 B/s";
    uint64_t last_bytes = 0;
    std::chrono::steady_clock::time_point last_time = std::chrono::steady_clock::now();
    std::string decision = "none"; // "none", "start", "overwrite", "resume", "re-transfer"
    bool is_directory = false;
    bool popped = false;
};

struct ActiveTransfer {
    std::string id;
    std::string direction; // "upload" or "download"
    std::string source;
    std::string destination;
    std::atomic<bool> active{true};
    std::atomic<bool> minimized{false};
    std::atomic<uint64_t> bytes_transferred{0};
    std::atomic<uint64_t> total_bytes{0};
    std::string current_file;
    std::string error_message;
    std::vector<FileProgress> files;
    std::mutex mutex;
    std::shared_ptr<client::Client> client_inst;
    std::thread thread;
    std::string session_id;
    std::string start_time;
    std::vector<std::string> session_ids;
    std::condition_variable cv_task;
    std::mutex task_mutex;
    std::atomic<bool> cancelled{false};
};

class HttpConnection; // forward declaration

class GuiServer {
    friend class HttpConnection; // HttpConnection dispatches into private handlers
public:
    GuiServer();
    ~GuiServer();

    bool start(uint16_t port);
    void stop();
    uint16_t get_port() const { return port_; }
    bool is_running() const { return running_; }

private:
    void do_accept();
    void send_response(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& status, const std::string& content_type, const std::string& body);
    void send_error(std::shared_ptr<asio::ip::tcp::socket> socket, int code, const std::string& message);

    // API handlers — called by HttpConnection
    void handle_api_drives(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handle_api_local_list(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& query);
    void handle_api_connect(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_remote_list(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& query);
    void handle_api_transfer(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_transfers(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handle_api_transfer_status(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& query);
    void handle_api_transfer_abort(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_transfer_minimize(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_transfer_file_control(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_transfer_remove(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_transfer_control(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_transfer_server_session(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& query);
    void handle_api_remote_check(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handle_api_disconnect(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handle_api_local_create_dir(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& query);
    void handle_api_remote_create_dir(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& query);
    void handle_api_profiles(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handle_api_profiles_save(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_profiles_delete(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_share_create(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& request_body);
    void handle_api_share_list(std::shared_ptr<asio::ip::tcp::socket> socket);
    void handle_shared_download(std::shared_ptr<asio::ip::tcp::socket> socket, const std::string& token);

    // Helper utilities
    std::string url_decode(const std::string& src);
    std::string get_query_param(const std::string& query, const std::string& param);
    std::string get_json_value(const std::string& json, const std::string& key);
    std::vector<std::string> get_json_array(const std::string& json, const std::string& key);

    uint16_t port_;
    std::atomic<bool> running_;
    std::unique_ptr<network::EventLoop> event_loop_;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;

    // Connection state
    std::mutex client_mutex_;
    std::shared_ptr<client::Client> active_client_;
    config::ClientConfig active_client_config_;
    crypto::SecurityLevel active_security_level_;
    bool remote_connected_;
    std::string remote_host_;
    uint16_t remote_port_;
    std::string remote_username_;
    std::vector<std::string> remote_allowed_paths_;

    // Active transfer progress state
    std::atomic<bool> transfer_active_;
    std::atomic<uint64_t> transfer_bytes_;
    std::atomic<uint64_t> transfer_total_bytes_;
    std::atomic<double> transfer_rate_;
    std::string transfer_rate_string_;
    std::string transfer_current_file_;
    std::string transfer_error_;
    
    // Background transfer management
    std::mutex transfer_mutex_;
    std::mutex transfers_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ActiveTransfer>> transfers_;

    struct SharedLink {
        std::string token;
        std::string file_path;
        uint64_t expires_at = 0;
        int download_count = 0;
        int max_downloads = 0;
    };
    std::mutex share_mutex_;
    std::unordered_map<std::string, SharedLink> shared_links_;
};

} // namespace gui
} // namespace netcopy
