#pragma once

#include "network/socket.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/crypto_engine.h"
#include "config/config_parser.h"
#include "protocol/message.h"
#include <memory>
#include <string>
#include <functional> // Added for std::function
#include <atomic>

namespace netcopy {
namespace client {

class Client {
public:
    Client();
    ~Client();
    
    // Configuration
    void load_config(const std::string& config_file);
    void set_config(const config::ClientConfig& config);
    const config::ClientConfig& get_config() const; // Added getter for config
    
    // Connection management
    void connect(const std::string& server_address, uint16_t port);
    void disconnect();
    bool is_connected() const;
    
    // Security settings
    void set_security_level(crypto::SecurityLevel level);
    
    // File transfer
    void transfer_file(const std::string& local_path, const std::string& remote_path, bool resume = false);
    void transfer_directory(const std::string& local_path, const std::string& remote_path, bool recursive = true, bool resume = false);
    
    // Progress callback
    using ProgressCallback = std::function<void(uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file)>;
    void set_progress_callback(ProgressCallback callback);
    
    // Error handling
    std::string get_last_error() const;

private:
    std::unique_ptr<network::Socket> socket_;
    std::unique_ptr<crypto::ChaCha20Poly1305> crypto_;
    std::unique_ptr<crypto::CryptoEngine> crypto_engine_;
    config::ClientConfig config_; 
    std::atomic<bool> connected_;
    std::string last_error_;
    ProgressCallback progress_callback_;
    uint32_t sequence_number_;
    crypto::SecurityLevel security_level_;
    crypto::SecurityLevel negotiated_security_level_;
    
    // Protocol handling
    void perform_handshake();
    void send_message(const protocol::Message& message);
    std::unique_ptr<protocol::Message> receive_message();
    
    // Encryption
    std::vector<uint8_t> encrypt_message(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decrypt_message(const std::vector<uint8_t>& data);
    
    // File transfer implementation
    void transfer_single_file(const std::string& local_path, const std::string& remote_path, bool resume);
    void send_file_data(const std::string& file_path, uint64_t resume_offset, uint64_t total_size);
    
    // Directory management
    void create_empty_directory(const std::string& remote_path);
    
    // Utility functions
    void set_error(const std::string& error);
    void clear_error();
    uint32_t get_next_sequence_number();
};

} // namespace client
} // namespace netcopy



