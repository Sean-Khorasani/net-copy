#pragma once

#include "network/socket.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/crypto_engine.h"
#include "config/config_parser.h"
#include "protocol/message.h"
#include "file/file_manager.h"
#include "auth/user_db.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

namespace netcopy {
namespace server {

struct ServerTransferSession;

class ConnectionHandler {
public:
    ConnectionHandler(network::Socket client_socket, 
                     const config::ServerConfig& config,
                     std::shared_ptr<crypto::ChaCha20Poly1305> crypto);
    ~ConnectionHandler();
    
    void handle();

private:
    network::Socket client_socket_;
    config::ServerConfig config_;
    std::shared_ptr<crypto::ChaCha20Poly1305> crypto_;
    std::unique_ptr<crypto::CryptoEngine> crypto_engine_;
    crypto::SecurityLevel negotiated_security_level_;
    uint32_t sequence_number_;
    std::string client_address_;
    std::string current_file_path_;  // Track the current file being transferred
    bool handshake_completed_;  // Track if handshake is done
    bool current_auto_create_;
    bool current_truncate_on_zero_;
    bool current_transfer_completed_;
    size_t negotiated_max_chunk_size_;
    file::FileStream current_file_stream_;
    bool current_is_symlink_ = false;
    std::string current_symlink_target_;
    uint32_t current_permissions_ = 0;
    uint64_t current_expected_file_size_ = 0;
    // Auth state
    auth::UserDb user_db_;
    std::string authenticated_user_;
    std::vector<uint8_t> session_shared_secret_;
    std::vector<uint8_t> client_nonce_from_handshake_;
    std::vector<uint8_t> server_nonce_from_handshake_;
    int auth_failure_count_ = 0;
    static constexpr int MAX_AUTH_FAILURES = 5;
    std::shared_ptr<ServerTransferSession> current_session_;
    
    // Protocol handling
    void perform_handshake();
    void handle_file_request(const protocol::FileRequest& request);
    void handle_file_data(const protocol::FileData& data);
    void handle_download_request(const protocol::DownloadRequest& request);
    void handle_list_request(const protocol::ListRequest& request);
    void handle_file_verify_request(const protocol::FileVerifyRequest& request);
    void handle_block_hashes_request(const protocol::BlockHashesRequest& request);
    void handle_transfer_status_request(const protocol::TransferStatusRequest& request);
    
    // Message handling
    void send_message(const protocol::Message& message);
    std::unique_ptr<protocol::Message> receive_message();
    
    // Encryption
    std::vector<uint8_t> encrypt_message(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decrypt_message(const std::vector<uint8_t>& data);
    
    // File operations
    bool is_path_allowed(const std::string& path);
    std::string resolve_path(const std::string& path);
    
    // Utility functions
    uint32_t get_next_sequence_number();
    std::string get_client_address();
};

class Server {
public:
    Server();
    ~Server();
    
    // Configuration
    void load_config(const std::string& config_file);
    void set_config(const config::ServerConfig& config);
    const config::ServerConfig& get_config() const;
    
    // Server operations
    void start();
    void stop();
    bool is_running() const;
    
    // Daemon operations
    void run_as_daemon();

private:
    std::unique_ptr<network::Socket> listen_socket_;
    config::ServerConfig config_;
    std::shared_ptr<crypto::ChaCha20Poly1305> crypto_;
    std::atomic<bool> running_;
    
    struct WorkerThread {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };
    std::vector<WorkerThread> worker_threads_;
    
    void accept_connections();
    void handle_client(network::Socket client_socket);
    void cleanup_threads(bool force_join_all = false);
};

} // namespace server
} // namespace netcopy

