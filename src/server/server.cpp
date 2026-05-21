#include "server/server.h"
#include "file/file_manager.h"
#include "logging/logger.h"
#include "common/utils.h"
#include "common/compression.h"
#include "daemon/daemon.h"
#include "exceptions.h"
#include "auth/user_db.h"
#include "auth/auth_engine.h"
#include "crypto/sha3.h"
#include <algorithm>
#include <vector> // Added for std::vector
#include <iostream> // Added for debug output

namespace netcopy {
namespace server {

// ConnectionHandler implementation
ConnectionHandler::ConnectionHandler(network::Socket client_socket, 
                                   const config::ServerConfig& config,
                                   std::shared_ptr<crypto::ChaCha20Poly1305> crypto)
    : client_socket_(std::move(client_socket)), config_(config), crypto_(crypto), 
      negotiated_security_level_(crypto::SecurityLevel::HIGH), sequence_number_(1), handshake_completed_(false),
      current_auto_create_(true), current_truncate_on_zero_(true), current_transfer_completed_(false),
      negotiated_max_chunk_size_(config.max_chunk_size) {
    client_address_ = get_client_address();
    
    // Load user database
    user_db_ = auth::UserDb::load(config_.users_file);
    if (user_db_.is_loaded()) {
        LOG_INFO("User database loaded from: " + config_.users_file + 
                 " (" + std::to_string(user_db_.users().size()) + " users)");
    }
    
    // Disable Nagle's algorithm and tune buffer sizes to 4MB for the server's client socket
    client_socket_.set_tcp_nodelay(true);
    client_socket_.set_buffer_sizes(4 * 1024 * 1024, 4 * 1024 * 1024);
}

ConnectionHandler::~ConnectionHandler() {
    current_file_stream_.close();
}

void ConnectionHandler::handle() {
    try {
        LOG_INFO("Handling connection from " + client_address_);
        
        perform_handshake();
        
        // Main message loop
        while (true) {
            auto message = receive_message();
            
            switch (message->get_type()) {
                case protocol::MessageType::FILE_REQUEST: {
                    auto request = dynamic_cast<protocol::FileRequest*>(message.get());
                    if (request) {
                        handle_file_request(*request);
                    }
                    break;
                }
                case protocol::MessageType::FILE_DATA: {
                    auto data = dynamic_cast<protocol::FileData*>(message.get());
                    if (data) {
                        handle_file_data(*data);
                    }
                    break;
                }
                default:
                    LOG_WARNING("Received unknown message type from " + client_address_);
                    break;
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Connection error with " + client_address_ + ": " + e.what());
    }
    
    LOG_INFO("Connection closed with " + client_address_);
}

void ConnectionHandler::perform_handshake() {
    // Receive handshake request
    auto request_msg = receive_message();
    auto request = dynamic_cast<protocol::HandshakeRequest*>(request_msg.get());
    if (!request) {
        throw ProtocolException("Invalid handshake request");
    }
    
    LOG_INFO("Handshake from client version: " + request->client_version);
    
    // Negotiate security level and maximum chunk size
    negotiated_security_level_ = request->security_level;
    negotiated_max_chunk_size_ = request->max_chunk_size == 0
        ? config_.max_chunk_size
        : (std::min)(config_.max_chunk_size, static_cast<size_t>(request->max_chunk_size));
    
    // Create appropriate crypto engine
    if (config_.require_auth && !config_.secret_key.empty()) {
        crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, config_.secret_key);
        std::string level_name;
        switch (negotiated_security_level_) {
            case crypto::SecurityLevel::HIGH:
                level_name = "HIGH (ChaCha20-Poly1305)";
                break;
            case crypto::SecurityLevel::FAST:
                level_name = "FAST (XOR cipher)";
                break;
            case crypto::SecurityLevel::AES:
                level_name = "AES (AES-CTR with hardware acceleration)";
                break;
        }
        LOG_INFO("Using security level: " + level_name);
    }
    
    // Send handshake response
    protocol::HandshakeResponse response;
    response.server_version = common::get_version_string();
    response.server_nonce = common::generate_random_bytes(16);
    response.authentication_required = config_.require_auth;
    response.accepted_security_level = negotiated_security_level_;
    response.max_chunk_size = negotiated_max_chunk_size_;
    
    send_message(response);
    
    // Auth phase
    bool auth_needed = false;
    if (user_db_.is_loaded()) {
        if (!request->username.empty()) {
            auth_needed = true;
        } else if (!config_.allow_anonymous) {
            throw AuthException("Anonymous access not allowed");
        }
    } else if (!config_.allow_anonymous && !config_.users_file.empty()) {
        // users_file configured but not found - allow only if allow_anonymous is true
        if (!config_.allow_anonymous) {
            LOG_WARNING("User database not found at '" + config_.users_file + "' - allowing anonymous (no auth)");
        }
    }

    if (auth_needed) {
        auth::AuthMethod method = static_cast<auth::AuthMethod>(request->auth_method_id);
        if (method == auth::AuthMethod::NONE) {
            if (!config_.allow_anonymous) {
                throw AuthException("Authentication required");
            }
        } else {
            auth::AuthEngine engine(user_db_);
            auto challenge = engine.prepare_challenge(request->username, method);

            // Build and send AuthChallenge message
            protocol::AuthChallenge auth_challenge_msg;
            auth_challenge_msg.method           = static_cast<uint8_t>(method);
            auth_challenge_msg.challenge_nonce  = challenge.challenge_nonce;
            auth_challenge_msg.salt_hex         = challenge.salt_hex;
            auth_challenge_msg.pbkdf2_iterations= challenge.pbkdf2_iterations;
            auth_challenge_msg.kem_ciphertext   = challenge.kem_ciphertext;
            auth_challenge_msg.mlkem_level_str  = crypto::MlKem::level_to_string(challenge.mlkem_level);
            auth_challenge_msg.kem_nonce        = challenge.kem_nonce;
            send_message(auth_challenge_msg);

            // Receive AuthResponse
            auto resp_msg = receive_message();
            auto auth_resp = dynamic_cast<protocol::AuthResponse*>(resp_msg.get());
            if (!auth_resp) {
                protocol::AuthResult fail;
                fail.success = false;
                fail.error_message = "Expected AuthResponse";
                send_message(fail);
                throw AuthException("Protocol error during authentication");
            }

            bool ok = engine.verify_response(challenge, auth_resp->proof);
            protocol::AuthResult result;
            result.success = ok;
            result.error_message = ok ? "" : "Invalid credentials";
            send_message(result);

            if (!ok) {
                throw AuthException("Authentication failed for user: " + request->username);
            }

            authenticated_user_ = request->username;
            LOG_INFO("User '" + request->username + "' authenticated successfully");
        }
    }

    handshake_completed_ = true;
    LOG_INFO("Handshake completed with " + client_address_);
}

void ConnectionHandler::handle_file_request(const protocol::FileRequest& request) {
    // Convert paths to native format for logging
    std::string native_source = common::convert_to_native_path(request.source_path);
    std::string native_dest = common::convert_to_native_path(request.destination_path);
    
    LOG_INFO("File request from " + client_address_ + ": " + native_source + " -> " + native_dest);
    
    protocol::FileResponse response;
    current_transfer_completed_ = false;
    
    try {
        // Validate destination path
        if (!is_path_allowed(request.destination_path)) {
            throw FileException("Access denied to path: " + request.destination_path);
        }
        
        // If a user is authenticated, also check their personal allowed_paths
        if (!authenticated_user_.empty() && user_db_.is_loaded()) {
            const auto* user = user_db_.find_user(authenticated_user_);
            if (user && !user->can_access_path(request.destination_path)) {
                throw FileException("User '" + authenticated_user_ + "' does not have access to: " + request.destination_path);
            }
        }
        
        std::string resolved_path = resolve_path(request.destination_path);
        
        // If destination is a directory, append the source filename
        if (resolved_path.back() == '/' || resolved_path.back() == '\\' || 
            file::FileManager::is_directory(resolved_path)) {
            std::string filename = file::FileManager::get_filename(request.source_path);
            resolved_path = file::FileManager::join_path(resolved_path, filename);
        }
        
        // Store current file path for use in file data handling
        current_file_path_ = resolved_path;
        LOG_DEBUG("Setting current file path to: " + current_file_path_);
        
        current_auto_create_ = request.auto_create_directories;
        current_truncate_on_zero_ = (request.resume_offset == 0);
        
        // Check if this is a resume request
        if (request.resume_offset > 0) {
            uint64_t current_size = file::FileManager::get_partial_file_size(resolved_path);
            response.resume_offset = current_size;
            LOG_DEBUG("Resume request for " + resolved_path + ", current size: " + std::to_string(current_size));
        } else {
            response.resume_offset = 0;
            LOG_DEBUG("New file transfer for " + resolved_path);
        }
        
        // Create directory if needed
        std::string dir = file::FileManager::get_directory(resolved_path);
        if (current_auto_create_ && !dir.empty() && !file::FileManager::exists(dir)) {
            file::FileManager::create_directories(dir);
            LOG_DEBUG("Created directory: " + dir);
        }
        
        response.success = true;
        response.file_size = 0; // Will be updated as we receive data
        
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
        LOG_ERROR("File request error: " + std::string(e.what()));
    }
    
    send_message(response);
}

void ConnectionHandler::handle_file_data(const protocol::FileData& data) {
    protocol::FileAck ack;
    bool message_completes_transfer = false;
    
    try {
        if (current_file_path_.empty()) {
            throw std::runtime_error("No file transfer in progress");
        }
        
        // Check if this is a directory marker file
        std::string filename = file::FileManager::get_filename(current_file_path_);
        bool is_marker_file = (filename == ".netcopy_dir_marker" || filename == ".netcopy_empty_dir");

        struct TempChunk {
            uint64_t offset;
            uint64_t uncompressed_size;
            const std::vector<uint8_t>& data;
            bool is_last_chunk;
            bool compressed;
        };

        std::vector<TempChunk> chunks_to_process;
        if (!data.chunks.empty()) {
            for (const auto& c : data.chunks) {
                chunks_to_process.push_back({c.offset, c.uncompressed_size, c.data, c.is_last_chunk, c.compressed});
            }
        } else {
            chunks_to_process.push_back({data.offset, data.uncompressed_size, data.data, data.is_last_chunk, data.compressed});
        }

        uint64_t max_bytes_received = 0;
        for (const auto& chunk : chunks_to_process) {
            LOG_DEBUG("Writing " + std::to_string(chunk.data.size()) + " bytes at offset " + 
                     std::to_string(chunk.offset) + " to file: " + current_file_path_);
            
            std::vector<uint8_t> payload = chunk.data;
            if (chunk.compressed) {
                payload = common::decompress_buffer(chunk.data, static_cast<size_t>(chunk.uncompressed_size));
            }

            if (is_marker_file) {
                // This is an empty directory marker - just ensure the directory exists, don't create the file
                if (!current_auto_create_) {
                    throw FileException("Server policy does not allow empty directory creation");
                }
                std::string dir = file::FileManager::get_directory(current_file_path_);
                if (!dir.empty() && !file::FileManager::exists(dir)) {
                    file::FileManager::create_directories(dir);
                    LOG_DEBUG("Created empty directory: " + dir);
                }
                LOG_DEBUG("Processed directory marker, directory created but marker file not saved");
            } else {
                if (!current_file_stream_.is_open() || current_file_stream_.get_path() != current_file_path_) {
                    current_file_stream_.close();
                    if (!current_file_stream_.open_write(current_file_path_, current_truncate_on_zero_, current_auto_create_)) {
                        throw FileException("Failed to open destination file for writing: " + current_file_path_);
                    }
                    current_truncate_on_zero_ = false;
                }
                
                current_file_stream_.write(chunk.offset, payload.data(), payload.size());
            }

            uint64_t end_offset = chunk.offset + payload.size();
            if (end_offset > max_bytes_received) {
                max_bytes_received = end_offset;
            }
            if (chunk.is_last_chunk) {
                message_completes_transfer = true;
            }
        }

        ack.bytes_received = max_bytes_received;
        ack.success = true;
        LOG_DEBUG("Successfully processed chunks, total bytes received: " + std::to_string(max_bytes_received));

    } catch (const std::exception& e) {
        current_file_stream_.close();
        ack.success = false;
        ack.error_message = e.what();
        LOG_ERROR("File data error: " + std::string(e.what()));
    }
    
    send_message(ack);
    if (ack.success && message_completes_transfer) {
        current_transfer_completed_ = true;
        current_file_stream_.close();
    }
}

void ConnectionHandler::send_message(const protocol::Message& message) {
    auto data = message.serialize();
    
    if (handshake_completed_ && (crypto_engine_ || crypto_)) {
        data = encrypt_message(data);
    }
    
    // Send message length first
    uint32_t length = static_cast<uint32_t>(data.size());
    client_socket_.send(&length, sizeof(length));
    
    // Send message data
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        size_t sent = client_socket_.send(data.data() + total_sent, data.size() - total_sent);
        total_sent += sent;
    }
}

std::unique_ptr<protocol::Message> ConnectionHandler::receive_message() {
    // Receive message length
    uint32_t length;
    client_socket_.receive(&length, sizeof(length));
    
    
    // Receive message data
    std::vector<uint8_t> data(length);
    size_t total_received = 0;
    while (total_received < length) {
        size_t received = client_socket_.receive(data.data() + total_received, length - total_received);
        total_received += received;
    }
    
    if (handshake_completed_ && (crypto_engine_ || crypto_)) {
        data = decrypt_message(data);
    }
    
    return protocol::Message::deserialize(data);
}

std::vector<uint8_t> ConnectionHandler::encrypt_message(const std::vector<uint8_t>& data) {
    if (crypto_engine_) {
        return crypto_engine_->encrypt(data);
    } else if (crypto_) {
        // Fallback to old ChaCha20 implementation
        auto nonce = crypto::ChaCha20Poly1305::generate_nonce();
        auto encrypted = crypto_->encrypt(data, nonce);
        
        // Prepend nonce to encrypted data
        std::vector<uint8_t> result;
        result.insert(result.end(), nonce.begin(), nonce.end());
        result.insert(result.end(), encrypted.begin(), encrypted.end());
        
        return result;
    } else {
        // No encryption
        return data;
    }
}

std::vector<uint8_t> ConnectionHandler::decrypt_message(const std::vector<uint8_t>& data) {
    if (crypto_engine_) {
        return crypto_engine_->decrypt(data);
    } else if (crypto_) {
        // Fallback to old ChaCha20 implementation
        if (data.size() < crypto::ChaCha20Poly1305::NONCE_SIZE + crypto::ChaCha20Poly1305::TAG_SIZE) {
            throw CryptoException("Encrypted message too short");
        }
        
        // Extract nonce
        crypto::ChaCha20Poly1305::Nonce nonce;
        std::copy(data.begin(), data.begin() + nonce.size(), nonce.begin());
        
        // Extract encrypted data (including tag)
        std::vector<uint8_t> encrypted_data(data.begin() + nonce.size(), data.end());
        
        // Extract tag from the end of encrypted data
        crypto::ChaCha20Poly1305::Tag tag;
        std::copy(encrypted_data.end() - tag.size(), encrypted_data.end(), tag.begin());
        
        return crypto_->decrypt(encrypted_data, nonce, tag);
    } else {
        // No encryption
        return data;
    }
}

bool ConnectionHandler::is_path_allowed(const std::string& path) {
    // Convert network path to native platform path for proper validation
    std::string native_path = netcopy::common::convert_to_native_path(path);
    std::string normalized = file::FileManager::normalize_path(native_path);
    
    LOG_DEBUG("Checking path access: '" + path + "' -> '" + native_path + "' -> '" + normalized + "'");
    
    // Check against all allowed paths
    for (const auto& allowed : config_.allowed_paths) {
        LOG_DEBUG("Checking against allowed path: '" + allowed + "'");
        if (file::FileManager::is_path_safe(normalized, allowed)) {
            LOG_DEBUG("Path allowed by allowed_paths rule: '" + allowed + "'");
            return true;
        }
    }
    
    LOG_DEBUG("Path denied - not within any allowed path");
    return false;
}

std::string ConnectionHandler::resolve_path(const std::string& path) {
    // Convert network path (always Unix-style) to native platform path
    std::string native_path = netcopy::common::convert_to_native_path(path);
    
    // Debug logging for path conversion
    LOG_DEBUG("Path conversion: '" + path + "' -> '" + native_path + "'");
    
    // All paths must be absolute now (no base directory for relative paths)
    if (netcopy::common::is_absolute_path(native_path)) {
        return file::FileManager::normalize_path(native_path);
    } else {
        throw FileException("Relative paths are not allowed. All paths must be absolute. Path: " + path);
    }
}

uint32_t ConnectionHandler::get_next_sequence_number() {
    return sequence_number_++;
}

std::string ConnectionHandler::get_client_address() {
    // This is a simplified implementation
    // In a real implementation, you would extract the actual client address
    return "client";
}

// Server implementation
Server::Server() : running_(false) {
    config_ = config::ServerConfig::get_default();
}

Server::~Server() {
    stop();
}

void Server::load_config(const std::string& config_file) {
    try {
        config_ = config::ServerConfig::load_from_file(config_file);
        
        // Initialize logging
        auto& logger = logging::Logger::instance();
        logger.set_level(logging::Logger::string_to_level(config_.log_level));
        logger.set_console_output(config_.console_output);
        if (!config_.log_file.empty()) {
            logger.set_file_output(config_.log_file);
        }
        
        // Initialize crypto if key is available
        if (!config_.secret_key.empty()) {
            try {
                std::string hex_key = config_.secret_key;
                // Remove "0x" prefix if present
                if (hex_key.length() > 2 && hex_key.substr(0, 2) == "0x") {
                    hex_key = hex_key.substr(2);
                }
                
                // Validate hex key length (should be 64 characters for 32 bytes)
                if (hex_key.length() != 64) {
                    throw std::runtime_error("Invalid secret key length. Expected 64 hex characters (32 bytes), got " + std::to_string(hex_key.length()));
                }
                
                auto key_bytes = common::from_hex_string(hex_key);
                crypto::ChaCha20Poly1305::Key key;
                std::copy(key_bytes.begin(), key_bytes.begin() + (std::min)(key_bytes.size(), key.size()), key.begin());
                crypto_ = std::make_shared<crypto::ChaCha20Poly1305>(key);
                
                LOG_DEBUG("Crypto initialized with secret key from config");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize crypto with secret key: " + std::string(e.what()));
                throw;
            }
        } else {
            LOG_DEBUG("No secret key found in config - will be prompted for password");
        }
        
        LOG_INFO("Server configuration loaded from: " + config_file);
        LOG_DEBUG("Config secret_key length: " + std::to_string(config_.secret_key.length()));
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load configuration: " + std::string(e.what()));
        throw;
    }
}

void Server::set_config(const config::ServerConfig& config) {
    config_ = config;
}

const config::ServerConfig& Server::get_config() const {
    return config_;
}

void Server::start() {
    try {
        LOG_INFO("Starting NetCopy server...");
        
        listen_socket_ = std::make_unique<network::Socket>();
#ifdef _WIN32
        // On Windows, use exclusive address to prevent multiple servers on same port
        listen_socket_->set_reuse_address(false);
#else
        // On Unix, SO_REUSEADDR helps with quick restarts
        listen_socket_->set_reuse_address(true);
#endif
        listen_socket_->bind(config_.listen_address, config_.listen_port);
        listen_socket_->listen(config_.max_connections);
        
        running_ = true;
        
        LOG_INFO("Securely listening on TCP port " + std::to_string(config_.listen_port));
        
        // Log all allowed paths
        if (config_.allowed_paths.empty()) {
            LOG_WARNING("No allowed paths configured - all access will be denied");
        } else {
            LOG_INFO("Allowed paths:");
            for (const auto& path : config_.allowed_paths) {
                LOG_INFO("  - " + path);
            }
        }
        
        accept_connections();
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start server: " + std::string(e.what()));
        throw;
    }
}

void Server::stop() {
    if (running_) {
        running_ = false;
        
        if (listen_socket_) {
            listen_socket_->close();
        }
        
        cleanup_threads();
        
        LOG_INFO("Server stopped");
    }
}

bool Server::is_running() const {
    return running_;
}

void Server::run_as_daemon() {
    daemon::Daemon::setup_signal_handlers();
    
    // Note: daemonize() is already called in main.cpp for immediate detachment
    
    if (!config_.pid_file.empty()) {
        daemon::Daemon::create_pid_file(config_.pid_file);
    }
    
    start();
}

void Server::accept_connections() {
    while (running_) {
        try {
            auto client_socket = listen_socket_->accept();
            
            // Create thread to handle client
            worker_threads_.emplace_back([this, client_socket = std::move(client_socket)]() mutable {
                handle_client(std::move(client_socket));
            });
            
            // Clean up finished threads
            cleanup_threads();
            
        } catch (const std::exception& e) {
            if (running_) {
                LOG_ERROR("Accept error: " + std::string(e.what()));
            }
        }
    }
}

void Server::handle_client(network::Socket client_socket) {
    try {
        ConnectionHandler handler(std::move(client_socket), config_, crypto_);
        handler.handle();
    } catch (const std::exception& e) {
        LOG_ERROR("Client handler error: " + std::string(e.what()));
    }
}

void Server::cleanup_threads() {
    worker_threads_.erase(
        std::remove_if(worker_threads_.begin(), worker_threads_.end(),
                      [](std::thread& t) {
                          if (t.joinable()) {
                              t.join();
                              return true;
                          }
                          return false;
                      }),
        worker_threads_.end());
}

} // namespace server
} // namespace netcopy

