#include "client/client.h"
#include "file/file_manager.h"
#include "logging/logger.h"
#include "common/utils.h"
#include "common/compression.h"
#include "exceptions.h"
#include "crypto/crypto_engine.h"
#include <filesystem>
#include <vector> // Added for std::vector
#include <algorithm> // Added for std::min
#include <functional> // Added for std::function
#include <set> // Added for std::set
#include <iostream> // Added for debug output

namespace netcopy {
namespace client {

Client::Client() 
    : connected_(false), sequence_number_(1), security_level_(crypto::SecurityLevel::HIGH),
      negotiated_security_level_(crypto::SecurityLevel::HIGH) {
    config_ = config::ClientConfig::get_default();
}

Client::~Client() {
    disconnect();
}

void Client::load_config(const std::string& config_file) {
    try {
        config_ = config::ClientConfig::load_from_file(config_file);
        
        // Initialize logging (console output will be configured later in main)
        auto& logger = logging::Logger::instance();
        logger.set_level(logging::Logger::string_to_level(config_.log_level));
        logger.set_console_output(config_.console_output);
        if (!config_.log_file.empty()) {
            logger.set_file_output(config_.log_file);
        }
        
        // Note: Configuration loaded message will be handled in main.cpp based on verbose flag
    } catch (const std::exception& e) {
        set_error("Failed to load configuration: " + std::string(e.what()));
        throw;
    }
}

void Client::set_config(const config::ClientConfig& config) {
    config_ = config;
}

const config::ClientConfig& Client::get_config() const {
    return config_;
}

void Client::connect(const std::string& server_address, uint16_t port) {
    try {
        clear_error();
        
        LOG_INFO("Connecting to " + server_address + ":" + std::to_string(port));
        
        socket_ = std::make_unique<network::Socket>();
        socket_->set_timeout(config_.timeout);
        socket_->connect(server_address, port);
        
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
                std::copy(key_bytes.begin(), key_bytes.begin() + std::min(key_bytes.size(), key.size()), key.begin());
                crypto_ = std::make_unique<crypto::ChaCha20Poly1305>(key);
                
                // Also create crypto engine for the requested security level
                crypto_engine_ = crypto::create_crypto_engine(security_level_, config_.secret_key);
                
                LOG_DEBUG("Crypto initialized with secret key from config");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to initialize crypto with secret key: " + std::string(e.what()));
                throw;
            }
        } else {
            LOG_DEBUG("No secret key found in config - connection may fail");
        }
        
        perform_handshake();
        connected_ = true;
        
        LOG_INFO("Successfully connected to server");
        
    } catch (const std::exception& e) {
        set_error("Connection failed: " + std::string(e.what()));
        disconnect();
        throw;
    }
}

void Client::disconnect() {
    if (socket_) {
        socket_->close();
        socket_.reset();
    }
    crypto_.reset();
    connected_ = false;
    LOG_INFO("Disconnected from server");
}

bool Client::is_connected() const {
    return connected_;
}

void Client::transfer_file(const std::string& local_path, const std::string& remote_path, bool resume) {
    if (!is_connected()) {
        throw NetworkException("Not connected to server");
    }
    
    if (!file::FileManager::exists(local_path)) {
        throw FileException("Local file does not exist: " + local_path);
    }
    
    if (file::FileManager::is_directory(local_path)) {
        throw FileException("Path is a directory, use transfer_directory instead: " + local_path);
    }
    
    transfer_single_file(local_path, remote_path, resume);
}

void Client::transfer_directory(const std::string& local_path, const std::string& remote_path, bool recursive, bool resume) {
    if (!is_connected()) {
        throw NetworkException("Not connected to server");
    }
    
    if (!file::FileManager::exists(local_path)) {
        throw FileException("Local directory does not exist: " + local_path);
    }
    
    if (!file::FileManager::is_directory(local_path)) {
        throw FileException("Path is not a directory: " + local_path);
    }
    
    auto files = file::FileManager::list_directory(local_path, recursive);
    
    // Get the source directory name to preserve structure
    std::string source_dir_name = file::FileManager::get_filename(local_path);
    std::string base_remote_path = file::FileManager::join_path(remote_path, source_dir_name);
    
    LOG_DEBUG("Transferring directory: " + local_path + " -> " + base_remote_path);
    LOG_DEBUG("Source directory name: " + source_dir_name);
    
    // First pass: Create all directories (including empty ones)
    std::set<std::string> directories_to_create;
    for (const auto& file_info : files) {
        if (file_info.is_directory) {
            std::filesystem::path relative_path = std::filesystem::relative(file_info.path, local_path);
            std::string remote_dir_path = file::FileManager::join_path(base_remote_path, relative_path.string());
            std::string network_dir_path = common::convert_to_unix_path(remote_dir_path);
            
            directories_to_create.insert(network_dir_path);
            LOG_DEBUG("Empty directory to create: " + network_dir_path);
        }
    }
    
    // Second pass: Transfer all files
    std::set<std::string> created_directories;
    for (const auto& file_info : files) {
        if (!file_info.is_directory) {
            std::filesystem::path relative_path = std::filesystem::relative(file_info.path, local_path);
            std::string remote_file_path = file::FileManager::join_path(base_remote_path, relative_path.string());
            
            // Convert path separators for cross-platform compatibility
            std::string network_path = common::convert_to_unix_path(remote_file_path);
            
            LOG_DEBUG("Transferring file: " + file_info.path + " -> " + network_path);
            transfer_single_file(file_info.path, network_path, resume);
            
            // Track which directory was created by this file and all its parent directories
            std::string file_dir = file::FileManager::get_directory(network_path);
            while (!file_dir.empty() && file_dir != "/" && file_dir != ".") {
                created_directories.insert(file_dir);
                // Also mark parent directories as created
                std::string parent_dir = file::FileManager::get_directory(file_dir);
                if (parent_dir == file_dir) break; // Avoid infinite loop
                file_dir = parent_dir;
            }
        }
    }
    
    // Third pass: Create any empty directories that weren't created by file transfers
    if (config_.create_empty_directories) {
        for (const std::string& dir_path : directories_to_create) {
            if (created_directories.find(dir_path) == created_directories.end()) {
                // This directory is empty, create it explicitly
                LOG_DEBUG("Creating empty directory: " + dir_path);
                create_empty_directory(dir_path);
            }
        }
    } else {
        LOG_DEBUG("Empty directory creation is disabled in configuration");
    }
}

void Client::create_empty_directory(const std::string& remote_path) {
    if (!is_connected()) {
        throw NetworkException("Not connected to server");
    }
    
    try {
        // Create a hidden marker file to ensure the directory exists
        std::string marker_file = remote_path;
        if (!marker_file.empty() && marker_file.back() != '/' && marker_file.back() != '\\') {
            marker_file += "/";
        }
        
        // Use a hidden filename on Windows (.filename) or a system filename on Unix
        if (common::is_windows_platform()) {
            marker_file += ".netcopy_empty_dir";  // Hidden file on Windows
        } else {
            marker_file += ".netcopy_empty_dir";  // Hidden file on Unix
        }
        
        LOG_DEBUG("Creating empty directory by sending hidden marker file: " + marker_file);
        
        // Send file request for the marker file
        protocol::FileRequest request;
        request.source_path = ".netcopy_dir_marker";  // Dummy source
        request.destination_path = marker_file;
        request.recursive = false;
        request.resume_offset = 0;
        
        send_message(request);
        
        auto response = receive_message();
        auto file_response = dynamic_cast<protocol::FileResponse*>(response.get());
        
        if (!file_response || !file_response->success) {
            std::string error = file_response ? file_response->error_message : "Unknown error";
            throw FileException("Failed to create directory: " + error);
        }
        
        // Send empty file data to create the marker file
        protocol::FileData data;
        data.offset = 0;
        data.data = {}; // Empty data
        
        send_message(data);
        
        auto ack_msg = receive_message();
        auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
        
        if (!ack || !ack->success) {
            std::string error = ack ? ack->error_message : "Unknown error";
            throw FileException("Failed to create directory marker: " + error);
        }
        
        LOG_DEBUG("Successfully created empty directory: " + remote_path);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create empty directory '" + remote_path + "': " + e.what());
        throw;
    }
}

void Client::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = callback;
}

std::string Client::get_last_error() const {
    return last_error_;
}

void Client::perform_handshake() {
    // Send handshake request
    protocol::HandshakeRequest request;
    request.client_version = common::get_version_string();
    request.client_nonce = common::generate_random_bytes(16);
    request.security_level = security_level_;
    
    send_message(request);
    
    // Receive handshake response
    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::HandshakeResponse*>(response_msg.get());
    if (!response) {
        throw ProtocolException("Invalid handshake response");
    }
    
    LOG_INFO("Handshake completed with server version: " + response->server_version);
    
    // Store negotiated security level
    negotiated_security_level_ = response->accepted_security_level;
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
        case crypto::SecurityLevel::AES_256_GCM:
            level_name = "AES-256-GCM (GPU accelerated)";
            break;
    }
    LOG_INFO("Negotiated security level: " + level_name);
    
    if (response->authentication_required) {
        if (!config_.secret_key.empty()) {
            // Use configured secret key
            crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, config_.secret_key);
            LOG_INFO("Authentication and secure channel creation done successfully with configured key.");
            
            // Log AES acceleration info if using AES
            if (negotiated_security_level_ == crypto::SecurityLevel::AES) {
                auto aes_engine = dynamic_cast<crypto::AesSecurityEngine*>(crypto_engine_.get());
                if (aes_engine) {
                    LOG_INFO("AES Hardware Status: " + aes_engine->get_acceleration_info());
                    if (aes_engine->is_using_hardware_acceleration()) {
                        LOG_INFO("AES-NI hardware acceleration is ACTIVE");
                    } else {
                        LOG_INFO("AES-NI hardware acceleration is NOT available - using software fallback");
                    }
                }
            }
            
            // Log GPU acceleration info if using AES-256-GCM
            if (negotiated_security_level_ == crypto::SecurityLevel::AES_256_GCM) {
                auto gpu_engine = dynamic_cast<crypto::GpuSecurityEngine*>(crypto_engine_.get());
                if (gpu_engine) {
                    LOG_INFO("GPU Status: " + gpu_engine->get_acceleration_info());
                    if (gpu_engine->is_using_gpu_acceleration()) {
                        LOG_INFO("GPU acceleration is ACTIVE");
                        auto metrics = gpu_engine->get_performance_metrics();
                        LOG_INFO("GPU Device: " + metrics.gpu_device_name);
                        LOG_INFO("Compute Capability: " + std::to_string(metrics.compute_capability_major) + 
                                "." + std::to_string(metrics.compute_capability_minor));
                    } else {
                        LOG_INFO("GPU acceleration is NOT available - using CPU fallback");
                    }
                }
            }
        } else if (!crypto_) {
            // Prompt for password if no key configured and no crypto initialized
            std::string password = common::get_password_from_console(
                "It seems you didn't configured the secret key in the client. Please enter the master password: ");
            
            // Create crypto engine based on negotiated security level
            crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, password);
            
            // Also create legacy crypto for backward compatibility
            auto salt = common::generate_random_bytes(32);
            auto key = crypto::ChaCha20Poly1305::derive_key(password, salt);
            crypto_ = std::make_unique<crypto::ChaCha20Poly1305>(key);
            
            LOG_INFO("Authentication and secure channel creation done successfully with password.");
        } else {
            // Use existing crypto but create engine for new security level
            crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, config_.secret_key);
            LOG_INFO("Authentication and secure channel creation done successfully with existing crypto.");
        }
    }
}

void Client::send_message(const protocol::Message& message) {
    auto data = message.serialize();
    
    // Don't encrypt handshake messages
    if (message.get_type() != protocol::MessageType::HANDSHAKE_REQUEST && 
        message.get_type() != protocol::MessageType::HANDSHAKE_RESPONSE) {
        if (crypto_engine_ || crypto_) {
            data = encrypt_message(data);
        }
    }
    
    // Send message length first
    uint32_t length = static_cast<uint32_t>(data.size());
    socket_->send(&length, sizeof(length));
    
    // Send message data
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        size_t sent = socket_->send(data.data() + total_sent, data.size() - total_sent);
        total_sent += sent;
    }
}

std::unique_ptr<protocol::Message> Client::receive_message() {
    // Receive message length
    uint32_t length;
    socket_->receive(&length, sizeof(length));
    
    // Receive message data
    std::vector<uint8_t> data(length);
    size_t total_received = 0;
    while (total_received < length) {
        size_t received = socket_->receive(data.data() + total_received, length - total_received);
        total_received += received;
    }
    
    if (crypto_engine_ || crypto_) {
        data = decrypt_message(data);
    }
    
    return protocol::Message::deserialize(data);
}

std::vector<uint8_t> Client::encrypt_message(const std::vector<uint8_t>& data) {
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

std::vector<uint8_t> Client::decrypt_message(const std::vector<uint8_t>& data) {
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

void Client::transfer_single_file(const std::string& local_path, const std::string& remote_path, bool resume) {
    uint64_t file_size = file::FileManager::file_size(local_path);
    uint64_t resume_offset = 0;
    
    // Send file request (with resume flag if needed)
    protocol::FileRequest request;
    request.source_path = local_path;
    request.destination_path = remote_path;
    request.recursive = false;
    request.resume_offset = resume ? 1 : 0; // Use 1 to indicate resume request
    
    send_message(request);
    
    // Receive file response
    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::FileResponse*>(response_msg.get());
    if (!response) {
        throw ProtocolException("Invalid file response");
    }
    
    if (!response->success) {
        throw FileException("Server error: " + response->error_message);
    }
    
    // Get resume offset from response
    resume_offset = response->resume_offset;
    
    LOG_DEBUG("Resume flag: " + std::string(resume ? "true" : "false") + ", resume_offset from server: " + std::to_string(resume_offset));
    
    if (resume && resume_offset > 0) {
        LOG_INFO("Resuming file transfer from offset " + std::to_string(resume_offset) + ": " + local_path + " -> " + common::convert_to_native_path(remote_path));
    } else {
        LOG_INFO("Starting file transfer: " + local_path + " -> " + common::convert_to_native_path(remote_path));
    }
    
    // Send file data
    send_file_data(local_path, resume_offset, file_size);
    
    LOG_INFO("File transfer completed: " + local_path);
}

void Client::send_file_data(const std::string& file_path, uint64_t resume_offset, uint64_t total_size) {
    uint64_t bytes_sent = resume_offset;
    bool compress = common::is_compressible(file_path);
    
    // Handle empty files (0 bytes) - still need to send one empty data chunk
    if (total_size == 0) {
        protocol::FileData data_msg;
        data_msg.offset = 0;
        data_msg.data = {}; // Empty data
        data_msg.is_last_chunk = true;
        
        send_message(data_msg);
        
        // Receive acknowledgment
        auto ack_msg = receive_message();
        auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
        if (!ack || !ack->success) {
            throw FileException("Transfer failed: " + (ack ? ack->error_message : "No acknowledgment"));
        }
        
        // Call progress callback for empty file
        if (progress_callback_) {
            progress_callback_(0, 0, file_path);
        }
        
        return;
    }
    
    while (bytes_sent < total_size) {
        size_t chunk_size = std::min(static_cast<uint64_t>(config_.buffer_size), total_size - bytes_sent);
        auto chunk_data = file::FileManager::read_file_chunk(file_path, bytes_sent, chunk_size);

        std::vector<uint8_t> payload = compress ? common::compress_buffer(chunk_data) : chunk_data;

        protocol::FileData data_msg;
        data_msg.offset = bytes_sent;
        data_msg.data = payload;
        data_msg.is_last_chunk = (bytes_sent + chunk_data.size() >= total_size);
        data_msg.compressed = compress;

        send_message(data_msg);
        
        // Receive acknowledgment
        auto ack_msg = receive_message();
        auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
        if (!ack || !ack->success) {
            throw FileException("Transfer failed: " + (ack ? ack->error_message : "No acknowledgment"));
        }
        
        bytes_sent += chunk_data.size();
        
        // Call progress callback
        if (progress_callback_) {
            progress_callback_(bytes_sent, total_size, file_path);
        }
        
        // Bandwidth limiting
        if (config_.max_bandwidth_percent < 100) {
            int delay_ms = static_cast<int>((100.0 / config_.max_bandwidth_percent - 1.0) * 10);
            common::sleep_milliseconds(delay_ms);
        }
    }
}

void Client::set_error(const std::string& error) {
    last_error_ = error;
    LOG_ERROR(error);
}

void Client::clear_error() {
    last_error_.clear();
}

uint32_t Client::get_next_sequence_number() {
    return sequence_number_++;
}

void Client::set_security_level(crypto::SecurityLevel level) {
    security_level_ = level;
}

} // namespace client
} // namespace netcopy

