#include "server/server.h"
#include "common/fast_mem.h"
#include "file/file_manager.h"
#include "logging/logger.h"
#include "common/utils.h"
#include "common/compression.h"
#include "daemon/daemon.h"
#include "exceptions.h"
#include "auth/user_db.h"
#include "auth/auth_engine.h"
#include "crypto/sha3.h"
#include "crypto/xxhash64.h"
#include "network/windows_experimental.h"
#include "logging/audit_log.h"
#include <algorithm>
#include <vector>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <random>
#include <unordered_map>
#include <mutex>
#include <thread>

namespace netcopy {
namespace server {

namespace {
uint64_t normalized_window_bytes(uint64_t configured) {
    constexpr uint64_t fallback = config::defaults::kDefaultInflightWindowBytes;
    constexpr uint64_t min_window = 4ull * 1024ull * 1024ull;
    constexpr uint64_t max_window = 512ull * 1024ull * 1024ull;
    uint64_t value = configured == 0 ? fallback : configured;
    return (std::max)(min_window, (std::min)(value, max_window));
}

uint64_t normalized_batch_bytes(uint64_t configured, uint64_t window_bytes) {
    constexpr uint64_t frame_margin = 1024ull * 1024ull;
    uint64_t value = configured == 0 ? config::defaults::kDefaultBatchBytes : configured;
    uint64_t max_frame_payload = config::defaults::kMaxFrameSize > frame_margin
        ? config::defaults::kMaxFrameSize - frame_margin
        : config::defaults::kMaxFrameSize;
    value = (std::min)(value, max_frame_payload);
    value = (std::min)(value, (std::max)(uint64_t(1), window_bytes / 2));
    return (std::max)(uint64_t(64 * 1024), value);
}

size_t normalized_batch_chunks(int configured) {
    int value = configured <= 0 ? config::defaults::kDefaultBatchChunks : configured;
    value = (std::max)(1, (std::min)(value, config::defaults::kMaxBatchChunks));
    return static_cast<size_t>(value);
}
}

struct ServerTransferSession {
    std::string session_id;
    std::string source_path;
    std::string destination_path;
    std::string owner_user; // empty if anonymous
    std::string owner_ip;
    std::atomic<uint64_t> bytes_transferred{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<bool> is_active{true};
    std::string status{"transferring"}; // "transferring", "completed", "failed", "aborted"
    std::vector<std::string> logs;
    std::mutex logs_mutex;
    std::chrono::system_clock::time_point start_time;
};

class SessionRegistry {
public:
    static SessionRegistry& instance() {
        static SessionRegistry reg;
        return reg;
    }
    
    std::shared_ptr<ServerTransferSession> create_session(
        const std::string& src, const std::string& dest,
        const std::string& user, const std::string& ip,
        uint64_t total_size) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto session = std::make_shared<ServerTransferSession>();
        session->session_id = generate_uuid();
        session->source_path = src;
        session->destination_path = dest;
        session->owner_user = user;
        session->owner_ip = ip;
        session->total_bytes = total_size;
        session->start_time = std::chrono::system_clock::now();
        
        {
            std::lock_guard<std::mutex> log_lock(session->logs_mutex);
            session->logs.push_back("Session started at " + format_time(session->start_time));
            session->logs.push_back("Source: " + src);
            session->logs.push_back("Destination: " + dest);
            session->logs.push_back("Owner: " + (user.empty() ? "anonymous" : user) + " (" + ip + ")");
        }
        
        sessions_[session->session_id] = session;
        return session;
    }
    
    std::shared_ptr<ServerTransferSession> get_session(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(id);
        if (it != sessions_.end()) {
            return it->second;
        }
        return nullptr;
    }
    
private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ServerTransferSession>> sessions_;
    
    std::string generate_uuid() {
        static const char hex[] = "0123456789abcdef";
        static std::mt19937 gen(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::uniform_int_distribution<> dis(0, 15);
        std::string s;
        s.reserve(16);
        for (int i = 0; i < 16; ++i) {
            s += hex[dis(gen)];
        }
        return s;
    }
    
    std::string format_time(std::chrono::system_clock::time_point tp) {
        std::time_t t = std::chrono::system_clock::to_time_t(tp);
        char buf[64];
        struct tm timeinfo;
#ifdef _WIN32
        localtime_s(&timeinfo, &t);
#else
        localtime_r(&t, &timeinfo);
#endif
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return buf;
    }
};

// Helper: derive session key from base key + ML-KEM material (Task 4)

// ConnectionHandler implementation
ConnectionHandler::ConnectionHandler(network::Socket client_socket, 
                                   const config::ServerConfig& config,
                                   std::shared_ptr<crypto::ChaCha20Poly1305> crypto)
    : client_socket_(std::move(client_socket)), config_(config), crypto_(crypto), 
      negotiated_security_level_(crypto::SecurityLevel::HIGH), sequence_number_(1), handshake_completed_(false),
      transport_encryption_active_(false), current_auto_create_(true), current_truncate_on_zero_(true), current_transfer_completed_(false),
      negotiated_max_chunk_size_(config.internal.max_chunk_size), current_is_symlink_(false), current_symlink_target_(""), current_permissions_(0), current_expected_file_size_(0), current_expected_last_modified_(0),
      cached_block_hash_valid_(false) {
    client_address_ = get_client_address();
    
    // Load user database
    user_db_ = auth::UserDb::load(config_.internal.users_file);
    if (user_db_.is_loaded()) {
        LOG_INFO("User database loaded from: " + config_.internal.users_file + 
                 " (" + std::to_string(user_db_.users().size()) + " users)");
    }
    
    // Disable Nagle's algorithm; respect configurable socket buffer sizes
    client_socket_.set_tcp_nodelay(true);
    if (config_.socket_buffer_size > 0) {
        client_socket_.set_buffer_sizes(config_.socket_buffer_size, config_.socket_buffer_size);
    }
    // If 0, leave OS default (auto-tuning)
}

ConnectionHandler::~ConnectionHandler() {
    current_file_stream_.close();
    if (current_session_ && current_session_->is_active) {
        current_session_->is_active = false;
        current_session_->status = "failed";
        std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
        current_session_->logs.push_back("Session terminated due to connection loss or early handler termination.");
    }
}

void ConnectionHandler::handle() {
    try {
        LOG_INFO("Handling connection from " + client_address_);
        
        if (config_.tls.enable) {
            LOG_INFO("Enabling TLS server-side for " + client_address_);
            client_socket_.enable_tls_server(config_.tls.server_cert_file, config_.tls.server_key_file, config_.tls.dh_file);
            LOG_INFO("Performing TLS handshake for " + client_address_);
            client_socket_.perform_tls_handshake();
            LOG_INFO("TLS handshake completed successfully for " + client_address_);
        }
        
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
                case protocol::MessageType::DOWNLOAD_REQUEST: {
                    auto req = dynamic_cast<protocol::DownloadRequest*>(message.get());
                    if (req) handle_download_request(*req);
                    break;
                }
                case protocol::MessageType::LIST_REQUEST: {
                    auto req = dynamic_cast<protocol::ListRequest*>(message.get());
                    if (req) handle_list_request(*req);
                    break;
                }
                case protocol::MessageType::FILE_VERIFY_REQUEST: {
                    auto req = dynamic_cast<protocol::FileVerifyRequest*>(message.get());
                    if (req) handle_file_verify_request(*req);
                    break;
                }
                case protocol::MessageType::BLOCK_HASHES_REQUEST: {
                    auto req = dynamic_cast<protocol::BlockHashesRequest*>(message.get());
                    if (req) handle_block_hashes_request(*req);
                    break;
                }
                case protocol::MessageType::DISCONNECT: {
                    LOG_INFO("Client " + client_address_ + " disconnected gracefully");
                    return;
                }
                case protocol::MessageType::TRANSFER_STATUS_REQUEST: {
                    auto req = dynamic_cast<protocol::TransferStatusRequest*>(message.get());
                    if (req) handle_transfer_status_request(*req);
                    break;
                }
                default:
                    LOG_WARNING("Received unknown message type from " + client_address_);
                    break;
            }
        }
        
    } catch (const std::exception& e) {
        std::string err_msg = e.what();
        if (err_msg == "Connection closed by peer") {
            LOG_INFO("Connection closed by peer " + client_address_);
        } else {
            LOG_ERROR("Connection error with " + client_address_ + ": " + err_msg);
            logging::AuditLog::instance().log_connect("", client_address_, false);
        }
    }
    
    LOG_INFO("Connection closed with " + client_address_);
    logging::AuditLog::instance().log_disconnect(authenticated_user_, client_address_);
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
    crypto::SecurityLevel configured_level = crypto::SecurityLevel::HIGH;
    bool enforce_level = false;
    
    std::string s_level = config_.internal.security_level;
    std::transform(s_level.begin(), s_level.end(), s_level.begin(), ::toupper);
    
    if (s_level != "AUTO" && !s_level.empty()) {
        enforce_level = true;
        if (s_level == "FAST") configured_level = crypto::SecurityLevel::FAST;
        else if (s_level == "AES" || s_level == "AES-CTR") configured_level = crypto::SecurityLevel::AES;
        else if (s_level == "AES-GCM" || s_level == "AES_256_GCM") configured_level = crypto::SecurityLevel::AES_256_GCM;
        else configured_level = crypto::SecurityLevel::HIGH;
    }
    
    negotiated_security_level_ = enforce_level ? configured_level : request->security_level;
    
    negotiated_max_chunk_size_ = request->max_chunk_size == 0
        ? config_.internal.max_chunk_size
        : (std::min)(config_.internal.max_chunk_size, static_cast<size_t>(request->max_chunk_size));
    
    // Create appropriate crypto engine
    if (config_.internal.require_auth && !config_.internal.secret_key.empty()) {
        crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, config_.internal.secret_key);
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
                level_name = "AES_256_GCM (AES-256-GCM authenticated encryption)";
                break;
        }
        LOG_INFO("Using security level: " + level_name);
    }
    
    // Send handshake response
    protocol::HandshakeResponse response;
    response.server_version = common::get_version_string();
    response.server_nonce = common::generate_random_bytes(16);
    response.authentication_required = config_.internal.require_auth;
    response.accepted_security_level = negotiated_security_level_;
    response.max_chunk_size = negotiated_max_chunk_size_;
    response.accepted_parallel_streams = request->requested_parallel_streams == 0 ? 1 : (std::min)(8u, request->requested_parallel_streams);
    response.auto_create_directories_allowed = config_.auto_create_directories;
    
    // Save nonces for session key derivation (Task 4)
    server_nonce_from_handshake_ = response.server_nonce;
    client_nonce_from_handshake_ = request->client_nonce;
    
    send_message(response);
    
    if (crypto_engine_ && !config_.internal.secret_key.empty()) {
        auto derived = common::derive_session_key(
            config_.internal.secret_key,
            {},
            server_nonce_from_handshake_,
            client_nonce_from_handshake_);
        std::string hex_derived = common::to_hex_string(derived);
        crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, "0x" + hex_derived);
        transport_encryption_active_ = true;
        LOG_DEBUG("Derived dynamic session key with nonces");
    }
    
    // Auth phase
    bool auth_needed = false;
    if (!config_.internal.allow_anonymous) {
        if (!user_db_.is_loaded()) {
            throw AuthException("Authentication required, but user database could not be loaded");
        }
        if (request->username.empty()) {
            throw AuthException("Anonymous access not allowed");
        }
        auth_needed = true;
    } else {
        // config_.internal.allow_anonymous is true
        if (user_db_.is_loaded() && !request->username.empty()) {
            auth_needed = true;
        }
    }

    if (auth_needed) {
        auth::AuthMethod method = static_cast<auth::AuthMethod>(request->auth_method_id);
        
        std::string s_auth = config_.internal.auth_method;
        std::transform(s_auth.begin(), s_auth.end(), s_auth.begin(), ::tolower);
        
        if (s_auth == "mlkem" && method != auth::AuthMethod::MLKEM) {
            throw AuthException("Server strictly requires ML-KEM authentication");
        } else if (s_auth == "password" && method != auth::AuthMethod::PASSWORD) {
            throw AuthException("Server strictly requires password authentication");
        }
        
        if (method == auth::AuthMethod::NONE) {
            if (!config_.internal.allow_anonymous) {
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

            if (!ok) {
                auth_failure_count_++;
                LOG_WARNING("Auth failure " + std::to_string(auth_failure_count_) +
                            "/" + std::to_string(MAX_AUTH_FAILURES) +
                            " from " + client_address_);
                protocol::AuthResult result_fail;
                result_fail.success = false;
                result_fail.error_message = "Invalid credentials";
                send_message(result_fail);
                if (auth_failure_count_ >= MAX_AUTH_FAILURES) {
                    throw AuthException("Too many authentication failures from " + client_address_);
                }
                throw AuthException("Authentication failed for user: " + request->username);
            }

            protocol::AuthResult result;
            result.success = true;
            result.error_message = "";
            send_message(result);

            authenticated_user_ = request->username;
            LOG_INFO("User '" + request->username + "' authenticated successfully");

            // Re-derive transport key mixing in the ML-KEM shared secret for forward secrecy
            if (method == auth::AuthMethod::MLKEM && crypto_engine_) {
                auto derived = common::derive_session_key(
                    config_.internal.secret_key,
                    challenge.kem_shared_secret,
                    server_nonce_from_handshake_,
                    client_nonce_from_handshake_);
                std::string hex_derived = common::to_hex_string(derived);
                crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, "0x" + hex_derived);
                LOG_DEBUG("Session key derived from ML-KEM shared material");
            }
            // Emit audit log connect on success
            logging::AuditLog::instance().log_connect(authenticated_user_, client_address_, true);
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
    current_file_stream_.close();
    
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
        block_hashes_were_computed_ = false;  // reset for the new file
        
        current_auto_create_ = request.auto_create_directories;
        current_truncate_on_zero_ = request.truncate_destination;
        current_is_symlink_ = request.is_symlink;
        current_symlink_target_ = request.symlink_target;
        current_permissions_ = request.permissions;
        current_expected_file_size_ = request.file_size;
        current_expected_last_modified_ = request.last_modified;
        current_preallocated_ = false;
        current_upload_hash_next_offset_ = 0;
        current_upload_hash_valid_ = config_.internal.streaming_verification && !request.is_symlink && request.resume_offset == 0;
        current_upload_hasher_ = current_upload_hash_valid_ ? std::make_unique<crypto::Sha3Hasher>() : nullptr;
        last_received_file_hash_valid_ = false;
        
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

        if (current_truncate_on_zero_ && !current_is_symlink_) {
            file::FileAccessPattern write_pattern = config_.internal.cache_hints
                ? file::FileAccessPattern::Random
                : file::FileAccessPattern::Normal;
            file::FileStream truncate_stream;
            if (!truncate_stream.open_write(resolved_path, true, current_auto_create_, write_pattern)) {
                throw FileException("Failed to truncate destination file for writing: " + resolved_path);
            }
            truncate_stream.close();
            current_truncate_on_zero_ = false;
            LOG_DEBUG("Truncated destination file before receiving ranged data: " + resolved_path);
        }
        
        response.success = true;
        if (!current_is_symlink_ && file::FileManager::exists(resolved_path) && file::FileManager::is_regular_file(resolved_path)) {
            response.file_size = file::FileManager::file_size(resolved_path);
        } else {
            response.file_size = 0;
        }
        
        // Create transaction session
        std::string client_ip = get_client_address();
        current_session_ = SessionRegistry::instance().create_session(
            request.source_path, request.destination_path,
            authenticated_user_, client_ip, request.file_size
        );
        response.session_id = current_session_->session_id;
        
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
        LOG_ERROR("File request error: " + std::string(e.what()));
        trigger_webhook("upload", request.source_path, request.destination_path, "failed", 0, e.what());
    }
    
    send_message(response);
}

void ConnectionHandler::handle_file_data(const protocol::FileData& data) {
    protocol::FileAck ack;
    bool message_completes_transfer = false;
    std::string filename;
    bool is_marker_file = false;
    if (!current_file_path_.empty()) {
        filename = file::FileManager::get_filename(current_file_path_);
        is_marker_file = (filename == ".netcopy_dir_marker" || filename == ".netcopy_empty_dir");
    }
    
    try {
        if (current_file_path_.empty()) {
            throw std::runtime_error("No file transfer in progress");
        }

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
        uint64_t chunk_total_payload_size = 0;
        for (const auto& chunk : chunks_to_process) {
            LOG_DEBUG("Writing " + std::to_string(chunk.data.size()) + " bytes at offset " + 
                     std::to_string(chunk.offset) + " to file: " + current_file_path_);
            
            std::vector<uint8_t> decompressed_payload;
            const uint8_t* payload_ptr = nullptr;
            size_t payload_size = 0;
            if (chunk.compressed) {
                decompressed_payload = common::decompress_buffer(chunk.data, static_cast<size_t>(chunk.uncompressed_size));
                payload_ptr = decompressed_payload.data();
                payload_size = decompressed_payload.size();
            } else {
                payload_ptr = chunk.data.data();
                payload_size = chunk.data.size();
            }

            if (current_is_symlink_) {
                std::error_code ec;
                if (std::filesystem::exists(current_file_path_, ec) || std::filesystem::is_symlink(current_file_path_, ec)) {
                    std::filesystem::remove(current_file_path_, ec);
                }
                if (!file::FileManager::create_symlink(current_symlink_target_, current_file_path_)) {
                    throw FileException("Failed to create symlink: " + current_file_path_ + " -> " + current_symlink_target_);
                }
                if (!file::FileManager::is_symlink(current_file_path_)) {
                    LOG_WARNING("Created placeholder file for symlink (privilege restrictions): " + current_file_path_ + " -> " + current_symlink_target_);
                } else {
                    LOG_DEBUG("Successfully created symlink: " + current_file_path_ + " -> " + current_symlink_target_);
                }
                if (current_permissions_ != 0) {
                    file::FileManager::set_permissions(current_file_path_, current_permissions_);
                }
            } else if (is_marker_file) {
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
                    file::FileAccessPattern write_pattern = config_.internal.cache_hints
                        ? file::FileAccessPattern::Random
                        : file::FileAccessPattern::Normal;
                    if (!current_file_stream_.open_write(current_file_path_, current_truncate_on_zero_, current_auto_create_, write_pattern)) {
                        throw FileException("Failed to open destination file for writing: " + current_file_path_);
                    }
                    if (config_.internal.preallocate_files && !current_preallocated_ && current_expected_file_size_ > 0) {
                        std::string prealloc_error;
                        if (!file::FileManager::preallocate_file(current_file_path_,
                                                                 current_expected_file_size_,
                                                                 current_auto_create_,
                                                                 config_.internal.trusted_skip_zero_fill,
                                                                 &prealloc_error)) {
                            LOG_WARNING("Upload preallocation skipped: " + prealloc_error);
                        }
                        current_preallocated_ = true;
                    }
                    current_truncate_on_zero_ = false;
                }

                if (current_upload_hasher_ && payload_size > 0) {
                    if (current_upload_hash_valid_ && chunk.offset == current_upload_hash_next_offset_) {
                        current_upload_hasher_->update(payload_ptr, payload_size);
                        current_upload_hash_next_offset_ += payload_size;
                    } else {
                        current_upload_hash_valid_ = false;
                    }
                }
                
                current_file_stream_.write(chunk.offset, payload_ptr, payload_size);
                
                // Invalidate the cached block full-hash when data is actually written.
                // The cached hash from handle_block_hashes_request represented the
                // file BEFORE modification; now it's stale.
                if (cached_block_hash_valid_ &&
                    file::FileManager::normalize_path(cached_block_hash_path_) == file::FileManager::normalize_path(current_file_path_)) {
                    cached_block_hash_valid_ = false;
                }
            }

            uint64_t end_offset = chunk.offset + payload_size;
            chunk_total_payload_size += payload_size;
            // Enforce max_file_size if configured (Task 3)
            if (config_.max_file_size > 0 && end_offset > config_.max_file_size) {
                throw FileException("File exceeds maximum allowed size of " +
                                    std::to_string(config_.max_file_size) + " bytes");
            }
            if (end_offset > max_bytes_received) {
                max_bytes_received = end_offset;
            }
            if (chunk.is_last_chunk) {
                message_completes_transfer = true;
            }
        }

        if (current_session_) {
            current_session_->bytes_transferred += chunk_total_payload_size;
        }

        ack.bytes_received = max_bytes_received;
        ack.success = true;
        LOG_DEBUG("Successfully processed chunks, total bytes received: " + std::to_string(max_bytes_received));

    } catch (const std::exception& e) {
        current_file_stream_.close();
        if (current_session_) {
            current_session_->is_active = false;
            current_session_->status = "failed";
            std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
            current_session_->logs.push_back("Transfer failed: " + std::string(e.what()));
        }
        ack.success = false;
        ack.error_message = e.what();
        LOG_ERROR("File data error: " + std::string(e.what()));
        trigger_webhook("upload", current_session_ ? current_session_->source_path : "", current_file_path_, "failed", current_session_ ? current_session_->bytes_transferred.load() : 0, e.what());
    }
    
    send_message(ack);
    if (ack.success && message_completes_transfer) {
        current_transfer_completed_ = true;
        current_file_stream_.close();
        if (current_session_) {
            current_session_->is_active = false;
            current_session_->status = "completed";
            std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
            current_session_->logs.push_back("Transfer completed successfully.");
        }
        if (!current_is_symlink_ && !is_marker_file) {
            std::error_code ec;
            uint64_t current_size = file::FileManager::exists(current_file_path_) ? file::FileManager::file_size(current_file_path_) : 0;
            if (current_size > current_expected_file_size_) {
                LOG_INFO("Truncating " + current_file_path_ + " from " + std::to_string(current_size) + " to " + std::to_string(current_expected_file_size_));
                std::filesystem::resize_file(current_file_path_, current_expected_file_size_, ec);
                if (ec) {
                    LOG_ERROR("Failed to truncate file: " + ec.message());
                }
            }
        }
        if (current_permissions_ != 0 && !current_is_symlink_) {
            file::FileManager::set_permissions(current_file_path_, current_permissions_);
        }
        if (current_expected_last_modified_ != 0 && !current_is_symlink_ && !is_marker_file) {
            try {
                file::FileManager::set_last_write_time(current_file_path_, current_expected_last_modified_);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to set last write time: " + std::string(e.what()));
            }
        }
        if (current_upload_hasher_ &&
            current_upload_hash_valid_ &&
            current_upload_hash_next_offset_ == current_expected_file_size_ &&
            !current_is_symlink_ &&
            !is_marker_file) {
            last_received_file_hash_ = current_upload_hasher_->finalize();
            last_received_file_hash_path_ = current_file_path_;
            last_received_file_hash_valid_ = true;
        }
        // Audit log the completed upload
        logging::AuditLog::instance().log_transfer(
            authenticated_user_, client_address_,
            current_file_path_, ack.bytes_received, 0.0, "", true);
        trigger_webhook("upload", current_session_ ? current_session_->source_path : "", current_file_path_, "success", ack.bytes_received);
    }
}

void ConnectionHandler::send_message(const protocol::Message& message) {
    auto data = message.serialize();
    
    if (transport_encryption_active_ && (crypto_engine_ || crypto_)) {
        data = encrypt_message(data);
    }
    if (data.size() > config::defaults::kMaxFrameSize) {
        throw ProtocolException("Server attempted to send a message exceeding the 64MB frame limit: " + std::to_string(data.size()) + " bytes");
    }
    
    // Send message length first (network byte order)
    uint32_t length = htonl(static_cast<uint32_t>(data.size()));
    client_socket_.send_vectored(&length, sizeof(length), data.data(), data.size());
}

std::unique_ptr<protocol::Message> ConnectionHandler::receive_message() {
    // Receive message length (network byte order)
    uint32_t length_net = 0;
    size_t length_received = 0;
    uint8_t* length_ptr = reinterpret_cast<uint8_t*>(&length_net);
    while (length_received < sizeof(length_net)) {
        size_t received = client_socket_.receive(length_ptr + length_received, sizeof(length_net) - length_received);
        if (received == 0) {
            throw NetworkException("Connection closed while receiving message length");
        }
        length_received += received;
    }
    uint32_t length = ntohl(length_net);
    
    // Safety check on length to avoid bad_alloc crash
    if (length > config::defaults::kMaxFrameSize) {
        throw ProtocolException("Server received a message with length exceeding the 64MB limit: " + std::to_string(length) + " bytes");
    }
    
    // Receive message data
    std::vector<uint8_t> data(length);
    size_t total_received = 0;
    while (total_received < length) {
        size_t received = client_socket_.receive(data.data() + total_received, length - total_received);
        if (received == 0) {
            throw NetworkException("Connection closed while receiving message data");
        }
        total_received += received;
    }
    
    if (transport_encryption_active_ && (crypto_engine_ || crypto_)) {
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
    return client_socket_.get_peer_address();
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
        logger.set_level(logging::Logger::string_to_level(config_.logging.level));
        logger.set_console_level(logging::Logger::string_to_level(config_.console.level));
        logger.set_console_output(config_.console.enable);
        logger.set_file_output(config_.logging.enable ? config_.logging.file : "");
        logger.set_json_format(config_.logging.format == config::defaults::kLogFormatJson);
        
        // Initialize audit log if configured
        if (!config_.logging.audit_file.empty()) {
            logging::AuditLog::instance().set_path(config_.logging.audit_file);
            LOG_INFO("Audit log: " + config_.logging.audit_file);
        }
        
        // Initialize crypto if key is available
        if (!config_.internal.secret_key.empty()) {
            try {
                std::string hex_key = config_.internal.secret_key;
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
        LOG_DEBUG("Config secret_key length: " + std::to_string(config_.internal.secret_key.length()));
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
        
        event_loop_ = std::make_unique<network::EventLoop>(config_.max_connections > 0 ? (std::min)(64, config_.max_connections) : std::thread::hardware_concurrency());
        event_loop_->start();
        
        asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string(config_.listen_address.empty() ? "0.0.0.0" : config_.listen_address), config_.listen_port);
        acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(event_loop_->get_io_context());
        
        acceptor_->open(endpoint.protocol());
        
#ifdef _WIN32
        // On Windows, use exclusive address to prevent multiple servers on same port
        acceptor_->set_option(asio::ip::tcp::acceptor::reuse_address(false));
#else
        // On Unix, SO_REUSEADDR helps with quick restarts
        acceptor_->set_option(asio::ip::tcp::acceptor::reuse_address(true));
#endif

        acceptor_->bind(endpoint);
        acceptor_->listen(config_.max_connections > 0 ? config_.max_connections : asio::socket_base::max_listen_connections);
        
        running_ = true;
        
        // Load user database and initialize AuthEngine for the optional SSH server
        user_db_ = auth::UserDb::load(config_.internal.users_file);
        auth_engine_ = std::make_unique<auth::AuthEngine>(user_db_);
        
        // Start secondary SSH/SFTP/SCP server listener if enabled
        if (config_.ssh.enable && !config_.tls.server_key_file.empty()) {
            try {
                ssh_server_ = std::make_unique<network::SshServer>(config_, *auth_engine_, config_.ssh.port);
                ssh_server_->start();
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to start optional secondary SSH server: " + std::string(e.what()));
            }
        }
        
        LOG_INFO("Securely listening on TCP port " + std::to_string(config_.listen_port) + " (async_accept)");
        
        // Log all allowed paths
        if (config_.allowed_paths.empty()) {
            LOG_WARNING("No allowed paths configured - all access will be denied");
        } else {
            LOG_INFO("Allowed paths:");
            for (const auto& path : config_.allowed_paths) {
                LOG_INFO("  - " + path);
            }
        }
        
        do_accept();
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start server: " + std::string(e.what()));
        throw;
    }
}

void Server::stop() {
    if (running_) {
        running_ = false;
        
        if (ssh_server_) {
            ssh_server_->stop();
            ssh_server_.reset();
        }
        
        if (acceptor_) {
            asio::error_code ec;
            acceptor_->close(ec);
            acceptor_.reset();
        }
        
        if (event_loop_) {
            event_loop_->stop();
            event_loop_.reset();
        }
        
        cleanup_threads(true); // Still clean up any remaining worker_threads if we had any
        
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

void Server::do_accept() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(event_loop_->get_io_context());
    acceptor_->async_accept(*socket, [this, socket](const asio::error_code& ec) {
        if (!running_) return;
        
        if (!ec) {
            auto native_handle = socket->release();
            network::Socket client_socket(static_cast<uint64_t>(native_handle));
            
            // Spawn a DEDICATED thread per connection.
            // ConnectionHandler::handle() runs a blocking synchronous recv() loop.
            // Posting it into the ASIO io_context pool would starve the pool:
            // with N concurrent streams all threads block on recv(), preventing
            // async_accept from firing for new parallel-stream connections.
            cleanup_threads();
            auto finished = std::make_shared<std::atomic<bool>>(false);
            std::thread t([this, client_sock = std::move(client_socket), finished]() mutable {
                handle_client(std::move(client_sock));
                finished->store(true);
            });
            {
                std::lock_guard<std::mutex> lock(worker_threads_mutex_);
                worker_threads_.push_back({std::move(t), finished});
            }
        } else {
            LOG_ERROR("Accept error: " + ec.message());
        }
        
        do_accept();
    });
}

void Server::handle_client(network::Socket client_socket) {
    try {
        ConnectionHandler handler(std::move(client_socket), config_, crypto_);
        handler.handle();
    } catch (const std::exception& e) {
        LOG_ERROR("Client handler error: " + std::string(e.what()));
    }
}

void Server::cleanup_threads(bool force_join_all) {
    std::lock_guard<std::mutex> lock(worker_threads_mutex_);
    worker_threads_.erase(
        std::remove_if(worker_threads_.begin(), worker_threads_.end(),
                      [force_join_all](WorkerThread& wt) {
                          if (wt.thread.joinable()) {
                              if (force_join_all || wt.finished->load()) {
                                  wt.thread.join();
                                  return true;
                              }
                          }
                          return false;
                      }),
        worker_threads_.end());
}

void ConnectionHandler::handle_download_request(const protocol::DownloadRequest& request) {
    protocol::DownloadResponse resp;
    std::string native_path = common::convert_to_native_path(request.remote_path);
    std::string resolved = file::FileManager::normalize_path(native_path);

    if (!is_path_allowed(request.remote_path)) {
        resp.success = false;
        resp.error_message = "Access denied: " + request.remote_path;
        resp.file_size = 0;
        resp.is_directory = false;
        send_message(resp);
        trigger_webhook("download", resolved, request.remote_path, "failed", 0, resp.error_message);
        return;
    }

    if (!file::FileManager::exists(resolved)) {
        resp.success = false;
        resp.error_message = "File not found: " + resolved;
        resp.file_size = 0;
        resp.is_directory = false;
        send_message(resp);
        trigger_webhook("download", resolved, request.remote_path, "failed", 0, resp.error_message);
        return;
    }

    resp.is_directory = file::FileManager::is_directory(resolved);
    resp.is_symlink = file::FileManager::is_symlink(resolved);
    if (resp.is_symlink) {
        resp.symlink_target = file::FileManager::read_symlink(resolved);
        resp.file_size = 0;
    } else {
        resp.file_size = resp.is_directory ? 0 : file::FileManager::file_size(resolved);
    }
    resp.permissions = file::FileManager::get_permissions(resolved);
    resp.last_modified = (resp.is_directory || resp.is_symlink) ? 0 : file::FileManager::last_write_time(resolved);
    resp.success = true;
    last_sent_file_hash_valid_ = false;

    // Create session
    std::string client_ip = get_client_address();
    current_session_ = SessionRegistry::instance().create_session(
        request.remote_path, request.remote_path,
        authenticated_user_, client_ip, resp.file_size
    );
    if (current_session_) {
        current_session_->bytes_transferred = request.resume_offset;
    }
    resp.session_id = current_session_->session_id;

    send_message(resp);

    if (!resp.is_directory && !resp.is_symlink) {
        file::FileStream fs;
        file::FileAccessPattern read_pattern = config_.internal.cache_hints
            ? file::FileAccessPattern::Sequential
            : file::FileAccessPattern::Normal;
        if (!fs.open_read(resolved, read_pattern)) {
            if (current_session_) {
                current_session_->is_active = false;
                current_session_->status = "failed";
                std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
                current_session_->logs.push_back("Failed to open file for reading.");
            }
            trigger_webhook("download", resolved, request.remote_path, "failed", 0, "Failed to open file for reading.");
            return;
        }
        const size_t CHUNK = 4 * 1024 * 1024;
        uint64_t offset = request.resume_offset;
        uint64_t file_size = resp.file_size;
        
        if (offset >= file_size) {
            // Send empty final chunk to signal completion when fully resumed
            protocol::FileData chunk_msg;
            chunk_msg.compressed = false;
            chunk_msg.offset = offset;
            chunk_msg.uncompressed_size = 0;
            chunk_msg.is_last_chunk = true;
            send_message(chunk_msg);
            
            auto ack_msg = receive_message();
            auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
            if (!ack || !ack->success) {
                LOG_ERROR("Final Download ACK failed");
            }
        } else {
            std::atomic<uint64_t> last_acknowledged_offset(offset);
            std::atomic<bool> ack_thread_failed(false);
            std::mutex ack_mutex;
            std::condition_variable ack_cv;
            uint64_t configured_window_bytes = normalized_window_bytes(config_.internal.inflight_window_bytes);
            if (config_.internal.tcp_info_window && !client_socket_.is_tls()) {
                configured_window_bytes = network::windows_experimental::recommended_tcp_inflight_window(
                    client_socket_.native_handle(),
                    configured_window_bytes);
            }
            const uint64_t max_window_bytes = configured_window_bytes;
            const uint64_t max_batch_bytes = normalized_batch_bytes(config_.internal.batch_bytes, configured_window_bytes);
            const size_t max_batch_chunks = normalized_batch_chunks(config_.internal.batch_chunks);
            crypto::Sha3Hasher download_hasher;
            bool download_hash_valid = config_.internal.streaming_verification && request.resume_offset == 0;
            
            std::thread ack_thread([&]() {
                try {
                    while (last_acknowledged_offset.load() < file_size && !ack_thread_failed.load()) {
                        auto ack_msg = receive_message();
                        auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
                        if (!ack || !ack->success) {
                            LOG_ERROR("Download ACK failed");
                            ack_thread_failed = true;
                            ack_cv.notify_all();
                            break;
                        }
                        
                        last_acknowledged_offset = ack->bytes_received;
                        ack_cv.notify_all();
                    }
                } catch (...) {
                    ack_thread_failed = true;
                    ack_cv.notify_all();
                }
            });

            while (offset < file_size && !ack_thread_failed.load()) {
                {
                    std::unique_lock<std::mutex> lock(ack_mutex);
                    ack_cv.wait(lock, [&]() {
                        return (offset - last_acknowledged_offset.load()) < max_window_bytes || ack_thread_failed.load();
                    });
                }
                
                if (ack_thread_failed.load()) break;

                protocol::FileData batch_msg;
                uint64_t batch_bytes = 0;
                size_t batch_count = 0;

                while (offset < file_size &&
                       batch_count < max_batch_chunks &&
                       batch_bytes < max_batch_bytes) {
                    size_t to_read = static_cast<size_t>((std::min)(static_cast<uint64_t>(CHUNK), file_size - offset));
                    protocol::FileData::Chunk chunk;
                    chunk.offset = offset;
                    chunk.data.resize(to_read);
                    size_t nr = fs.read(offset, chunk.data.data(), to_read);
                    if (nr == 0) {
                        break;
                    }
                    chunk.data.resize(nr);
                    chunk.uncompressed_size = nr;
                    chunk.compressed = false;
                    chunk.is_last_chunk = (offset + nr >= file_size);

                    if (download_hash_valid) {
                        download_hasher.update(chunk.data.data(), chunk.data.size());
                    }

                    offset += nr;
                    batch_bytes += nr;
                    ++batch_count;
                    batch_msg.chunks.push_back(std::move(chunk));

                    if (batch_msg.chunks.back().is_last_chunk) {
                        break;
                    }
                }

                if (batch_msg.chunks.empty()) {
                    break;
                }
                
                if (batch_msg.chunks.size() == 1) {
                    auto only = std::move(batch_msg.chunks.front());
                    batch_msg.chunks.clear();
                    batch_msg.offset = only.offset;
                    batch_msg.uncompressed_size = only.uncompressed_size;
                    batch_msg.data = std::move(only.data);
                    batch_msg.compressed = only.compressed;
                    batch_msg.is_last_chunk = only.is_last_chunk;
                }

                send_message(batch_msg);
                
                if (current_session_) {
                    current_session_->bytes_transferred = offset;
                }
            }
            
            if (ack_thread.joinable()) {
                ack_thread.join();
            }
            
            if (ack_thread_failed.load()) {
                if (current_session_) {
                    current_session_->is_active = false;
                    current_session_->status = "failed";
                    std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
                    current_session_->logs.push_back("Download failed: missing or invalid ACK from client.");
                }
                trigger_webhook("download", resolved, request.remote_path, "failed", offset, "Download failed: missing or invalid ACK from client.");
            }
            if (!ack_thread_failed.load() && download_hash_valid && offset >= file_size) {
                last_sent_file_hash_ = download_hasher.finalize();
                last_sent_file_hash_path_ = resolved;
                last_sent_file_hash_valid_ = true;
            } else {
                last_sent_file_hash_valid_ = false;
            }
        }
        fs.close();

        if (offset >= file_size) {
            LOG_INFO("Download completed: " + resolved + " (" + std::to_string(offset) + " bytes)");
            if (current_session_) {
                current_session_->is_active = false;
                current_session_->status = "completed";
                std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
                current_session_->logs.push_back("Download completed successfully.");
            }
            trigger_webhook("download", resolved, request.remote_path, "success", offset);
        }
    } else if (resp.is_symlink) {
        // Send a single empty chunk to complete the flow
        protocol::FileData chunk_msg;
        chunk_msg.offset = 0;
        chunk_msg.uncompressed_size = 0;
        chunk_msg.data = std::vector<uint8_t>();
        chunk_msg.compressed = false;
        chunk_msg.is_last_chunk = true;
        send_message(chunk_msg);
        
        auto ack_msg = receive_message();
        auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
        if (!ack || !ack->success) {
            LOG_ERROR("Download symlink ACK failed");
            if (current_session_) {
                current_session_->is_active = false;
                current_session_->status = "failed";
                std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
                current_session_->logs.push_back("Download symlink failed: invalid ACK from client.");
            }
            trigger_webhook("download", resolved, request.remote_path, "failed", 0, "Download symlink failed: invalid ACK from client.");
        } else {
            if (current_session_) {
                current_session_->is_active = false;
                current_session_->status = "completed";
                std::lock_guard<std::mutex> log_lock(current_session_->logs_mutex);
                current_session_->logs.push_back("Download symlink completed successfully.");
            }
            trigger_webhook("download", resolved, request.remote_path, "success", 0);
        }
        LOG_INFO("Download symlink completed: " + resolved);
    }
}

void ConnectionHandler::handle_list_request(const protocol::ListRequest& request) {
    protocol::ListResponse resp;
    std::string native_path = common::convert_to_native_path(request.remote_path);
    std::string resolved = file::FileManager::normalize_path(native_path);

    if (!is_path_allowed(request.remote_path)) {
        resp.success = false;
        resp.error_message = "Access denied: " + request.remote_path;
        send_message(resp);
        return;
    }

    if (!file::FileManager::exists(resolved)) {
        resp.success = false;
        resp.error_message = "Path not found: " + resolved;
        send_message(resp);
        return;
    }

    try {
        auto entries = file::FileManager::list_directory(resolved, request.recursive);
        resp.success = true;
        for (const auto& e : entries) {
            protocol::RemoteFileInfo info;
            info.path = e.path;
            info.size = e.size;
            info.is_directory = e.is_directory;
            info.last_modified = e.last_modified;
            info.permissions = e.permissions;
            info.is_symlink = e.is_symlink;
            info.symlink_target = e.symlink_target;
            resp.entries.push_back(std::move(info));
        }
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error_message = e.what();
    }
    send_message(resp);
}

void ConnectionHandler::handle_file_verify_request(const protocol::FileVerifyRequest& request) {
    protocol::FileVerifyResponse response;
    response.success = false;
    
    try {
        std::string resolved = resolve_path(request.file_path);
        
        if (!is_path_allowed(request.file_path)) {
            throw FileException("Access denied: " + request.file_path);
        }
        
        if (!file::FileManager::exists(resolved)) {
            throw FileException("File not found: " + resolved);
        }
        
        std::vector<uint8_t> actual_hash;
        if (config_.internal.streaming_verification &&
            last_received_file_hash_valid_ &&
            file::FileManager::normalize_path(last_received_file_hash_path_) == resolved) {
            actual_hash = last_received_file_hash_;
            LOG_INFO("Using streamed upload checksum for E2E integrity check of " + resolved);
        } else if (config_.internal.streaming_verification &&
                   last_sent_file_hash_valid_ &&
                   file::FileManager::normalize_path(last_sent_file_hash_path_) == resolved) {
            actual_hash = last_sent_file_hash_;
            LOG_INFO("Using streamed download checksum for E2E integrity check of " + resolved);
        } else if (cached_block_hash_valid_ &&
                   file::FileManager::normalize_path(cached_block_hash_path_) == resolved) {
            // Reuse the full-file hash already computed during handle_block_hashes_request.
            // This is valid when all blocks matched (no delta writes occurred), avoiding
            // a redundant pass over the file.
            actual_hash = cached_block_full_hash_;
            LOG_INFO("Using cached block-hash checksum for E2E integrity check of " + resolved);
        } else if (block_hashes_were_computed_ &&
                   file::FileManager::normalize_path(cached_block_hash_path_) == resolved) {
            // Delta-sync: block-level integrity was already verified by
            // BlockHashesRequest + per-chunk comparison.  The full-file
            // re-read would steal disk I/O from concurrent transfers for
            // no benefit.  Trust the client-provided hash.
            actual_hash = request.expected_hash;
            LOG_INFO("Block-level integrity already verified for delta-sync; accepting client hash for " + resolved);
        } else {
            LOG_INFO("Computing checksum for E2E integrity check of " + resolved);
            actual_hash = file::FileManager::compute_file_hash(resolved);
        }
        response.actual_hash = actual_hash;
        
        if (actual_hash == request.expected_hash) {
            response.success = true;
            LOG_INFO("E2E integrity check successful for " + resolved);
        } else {
            response.success = false;
            response.error_message = "Integrity check failed: hash mismatch";
            LOG_ERROR("E2E integrity check failed for " + resolved + " - hash mismatch!");
        }
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
        LOG_ERROR("E2E verification error: " + std::string(e.what()));
    }
    
    send_message(response);
}

void ConnectionHandler::handle_block_hashes_request(const protocol::BlockHashesRequest& request) {
    protocol::BlockHashesResponse response;
    response.success = false;
    response.block_size = request.block_size;
    
    try {
        std::string resolved = resolve_path(request.file_path);
        
        if (!is_path_allowed(request.file_path)) {
            throw FileException("Access denied: " + request.file_path);
        }
        
        if (!file::FileManager::exists(resolved)) {
            throw FileException("File not found: " + resolved);
        }
        
        current_truncate_on_zero_ = false;
        
        LOG_INFO("Computing block hashes for " + resolved + " with block size " + std::to_string(request.block_size));
        
        // Compute block hashes AND full-file hash in one pass (xxHash64).
        // Cache the full-file hash so handle_file_verify_request can skip
        // a redundant re-hash when no delta writes occurred.
        std::vector<uint8_t> full_hash;
        auto file_hashes = file::FileManager::compute_block_hashes(resolved, request.block_size, {}, &full_hash);
        
        // Cache the full-file hash for potential E2E verify reuse
        cached_block_full_hash_ = std::move(full_hash);
        cached_block_hash_path_ = resolved;
        cached_block_hash_valid_ = true;
        block_hashes_were_computed_ = true;
        
        response.blocks.clear();
        for (const auto& bh : file_hashes) {
            protocol::BlockHashInfo info;
            info.offset = bh.offset;
            info.hash = bh.hash;
            response.blocks.push_back(std::move(info));
        }
        response.success = true;
    } catch (const std::exception& e) {
        response.success = false;
        response.error_message = e.what();
        LOG_ERROR("Block hashing error: " + std::string(e.what()));
    }
    
    send_message(response);
}

void ConnectionHandler::handle_transfer_status_request(const protocol::TransferStatusRequest& request) {
    protocol::TransferStatusResponse response;
    response.success = false;

    auto session = SessionRegistry::instance().get_session(request.session_id);
    if (!session) {
        response.error_message = "Session not found: " + request.session_id;
        send_message(response);
        return;
    }

    // Access control:
    bool is_owner = false;
    if (!session->owner_user.empty()) {
        is_owner = (session->owner_user == authenticated_user_);
    } else {
        auto get_ip_only = [](const std::string& addr) {
            size_t colon = addr.find_last_of(':');
            if (colon != std::string::npos) {
                if (addr.front() == '[' && addr[colon - 1] == ']') {
                    return addr.substr(1, colon - 2);
                }
                return addr.substr(0, colon);
            }
            return addr;
        };
        is_owner = authenticated_user_.empty() && (get_ip_only(get_client_address()) == get_ip_only(session->owner_ip));
    }

    if (!is_owner) {
        response.error_message = "Access denied to session info: " + request.session_id;
        send_message(response);
        return;
    }

    response.success = true;
    response.active = session->is_active.load();
    response.bytes_transferred = session->bytes_transferred.load();
    response.total_bytes = session->total_bytes.load();
    response.status_string = session->status;

    std::string all_logs;
    {
        std::lock_guard<std::mutex> log_lock(session->logs_mutex);
        for (const auto& log_line : session->logs) {
            all_logs += log_line + "\n";
        }
    }
    response.logs = all_logs;

    send_message(response);
}

void ConnectionHandler::trigger_webhook(const std::string& action, const std::string& source, const std::string& destination, const std::string& status, uint64_t bytes, const std::string& error_msg, uint32_t files_transferred) {
    if (config_.webhook_url.empty()) {
        return;
    }
    std::string url = config_.webhook_url;
    std::string session_id = current_session_ ? current_session_->session_id : "";
    
    std::string payload = "{";
    payload += "\"session_id\":\"" + common::escape_json(session_id) + "\",";
    payload += "\"action\":\"" + common::escape_json(action) + "\",";
    payload += "\"status\":\"" + common::escape_json(status) + "\",";
    payload += "\"source\":\"" + common::escape_json(source) + "\",";
    payload += "\"destination\":\"" + common::escape_json(destination) + "\",";
    payload += "\"files_transferred\":" + std::to_string(files_transferred) + ",";
    payload += "\"total_bytes\":" + std::to_string(bytes) + ",";
    payload += "\"error_message\":\"" + common::escape_json(error_msg) + "\"";
    payload += "}";

    std::thread([url, payload]() {
        common::send_http_post(url, payload);
    }).detach();
}

void Server::run_relay_client(const std::string& relay_address, const std::string& token) {
    LOG_INFO("Starting server in relay-client mode connecting to: " + relay_address + " with token " + token);
    running_ = true;
    
    std::string host;
    uint16_t port = 1245;
    size_t colon = relay_address.find(':');
    if (colon != std::string::npos) {
        host = relay_address.substr(0, colon);
        port = static_cast<uint16_t>(std::stoi(relay_address.substr(colon + 1)));
    } else {
        host = relay_address;
    }
    
    while (running_) {
        try {
            network::Socket relay_sock;
            if (config_.udp) {
                relay_sock.set_udp(true);
            }
            relay_sock.set_timeout(30); // 30s connection timeout
            relay_sock.connect(host, port);
            
            // Send RELAY_REGISTER token
            std::string reg_cmd = "RELAY_REGISTER " + token + "\n";
            relay_sock.send(reg_cmd.data(), reg_cmd.size());
            
            // Wait for data (which indicates client paired/connected)
            relay_sock.set_timeout(0); // Infinite read timeout
            
            // Create a worker thread to handle the client connection
            auto finished = std::make_shared<std::atomic<bool>>(false);
            std::thread t([this, sock = std::move(relay_sock), finished]() mutable {
                handle_client(std::move(sock));
                finished->store(true);
            });
            worker_threads_.push_back({std::move(t), finished});
            
            // Clean up completed worker threads to avoid resource leak
            cleanup_threads(false);
            
            // Wait a tiny bit before establishing the next registration socket
            // so we don't connect in an infinite tight loop if relay is down
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (const std::exception& e) {
            LOG_ERROR("Relay connection error: " + std::string(e.what()) + ". Retrying in 5 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

void Server::run_relay_server(const std::string& listen_address) {
    LOG_INFO("Starting relay pairing server listening on: " + listen_address);
    running_ = true;
    
    std::string host;
    uint16_t port = 1245;
    size_t colon = listen_address.find(':');
    if (colon != std::string::npos) {
        host = listen_address.substr(0, colon);
        port = static_cast<uint16_t>(std::stoi(listen_address.substr(colon + 1)));
    } else {
        host = listen_address;
    }
    
    network::Socket listen_sock;
    if (config_.udp) {
        listen_sock.set_udp(true);
    }
    listen_sock.bind(host, port);
    listen_sock.listen(100);
    
    // We maintain paired connection states.
    // Map of token -> Server Socket
    std::mutex registry_mutex;
    std::unordered_map<std::string, network::Socket> server_registry;
    
    auto bridge_sockets = [](network::Socket s1, network::Socket s2) {
        // Use shared_ptr so Socket handles don't close prematurely
        auto s1_shared = std::make_shared<network::Socket>(std::move(s1));
        auto s2_shared = std::make_shared<network::Socket>(std::move(s2));
        
        // Run forwarding in two separate threads
        auto fwd = [](std::shared_ptr<network::Socket> from, std::shared_ptr<network::Socket> to) {
            std::vector<char> buffer(65536);
            try {
                while (true) {
                    int bytes_read = from->receive(buffer.data(), static_cast<int>(buffer.size()));
                    if (bytes_read <= 0) break;
                    int bytes_sent = 0;
                    while (bytes_sent < bytes_read) {
                        int sent = to->send(buffer.data() + bytes_sent, static_cast<int>(bytes_read - bytes_sent));
                        if (sent <= 0) break;
                        bytes_sent += sent;
                    }
                    if (bytes_sent < bytes_read) break;
                }
            } catch (...) {}
            from->close();
            to->close();
        };
        
        std::thread t1([s1_shared, s2_shared, fwd]() { fwd(s1_shared, s2_shared); });
        std::thread t2([s2_shared, s1_shared, fwd]() { fwd(s2_shared, s1_shared); });
        t1.detach();
        t2.detach();
    };
    
    while (running_) {
        try {
            network::Socket incoming = listen_sock.accept();
            incoming.set_timeout(5); // 5s timeout to read registration header
            
            // Read first line
            std::string header;
            char ch;
            while (incoming.receive(&ch, 1) == 1) {
                if (ch == '\n') break;
                header += ch;
                if (header.size() > 1024) break; // sanity limit
            }
            
            incoming.set_timeout(0); // reset timeout
            
            if (header.rfind("RELAY_REGISTER ", 0) == 0) {
                std::string token = header.substr(15);
                // Trim trailing \r if any
                if (!token.empty() && token.back() == '\r') token.pop_back();
                
                LOG_INFO("Relay: Registered server for token '" + token + "'");
                std::lock_guard<std::mutex> lock(registry_mutex);
                // If there's an existing server socket, close it
                auto it = server_registry.find(token);
                if (it != server_registry.end()) {
                    it->second.close();
                }
                server_registry[token] = std::move(incoming);
            } else if (header.rfind("RELAY_CONNECT ", 0) == 0) {
                std::string token = header.substr(14);
                if (!token.empty() && token.back() == '\r') token.pop_back();
                
                network::Socket server_sock;
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(registry_mutex);
                    auto it = server_registry.find(token);
                    if (it != server_registry.end()) {
                        server_sock = std::move(it->second);
                        server_registry.erase(it);
                        found = true;
                    }
                }
                
                if (found) {
                    LOG_INFO("Relay: Paired client and server for token '" + token + "'");
                    bridge_sockets(std::move(server_sock), std::move(incoming));
                } else {
                    LOG_WARNING("Relay: Client requested token '" + token + "' but no server is registered");
                    incoming.close();
                }
            } else {
                LOG_WARNING("Relay: Invalid connection protocol: '" + header + "'");
                incoming.close();
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Relay server accept loop error: " + std::string(e.what()));
        }
    }
}

} // namespace server
} // namespace netcopy
