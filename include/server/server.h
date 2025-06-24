#pragma once

#include "network/socket.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/crypto_engine.h"
#include "config/config_parser.h"
#include "protocol/message.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

namespace netcopy {
namespace server {

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
    
    // Protocol handling
    void perform_handshake();
    void handle_file_request(const protocol::FileRequest& request);
    void handle_file_data(const protocol::FileData& data);
    
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
    std::vector<std::thread> worker_threads_;
    
    void accept_connections();
    void handle_client(network::Socket client_socket);
    void cleanup_threads();
};

} // namespace server
} // namespace netcopy

