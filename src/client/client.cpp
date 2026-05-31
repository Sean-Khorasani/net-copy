#include "client/client.h"
#include "common/fast_mem.h"
#include "common/compression.h"
#include "common/utils.h"
#include "exceptions.h"
#include "file/file_manager.h"
#include "logging/logger.h"
#include "crypto/sha3.h"
#include "crypto/mlkem.h"
#include "crypto/key_manager.h"
#include "protocol/message.h"
#include "network/windows_experimental.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <algorithm>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include <future>

namespace netcopy {
namespace client {

namespace {
template <typename Func>
size_t execute_io_sync(Func&& async_op) {
    auto promise = std::make_shared<std::promise<size_t>>();
    auto future = promise->get_future();
    async_op([promise](const asio::error_code& ec, size_t bytes) {
        if (ec) {
            promise->set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        } else {
            promise->set_value(bytes);
        }
    });
    return future.get();
}

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

Client::Client()
    : config_(config::ClientConfig::get_default()),
      connected_(false),
      sequence_number_(1),
      security_level_(crypto::SecurityLevel::HIGH),
      negotiated_security_level_(crypto::SecurityLevel::HIGH),
      negotiated_max_chunk_size_(config_.internal.max_chunk_size),
      requested_parallel_streams_((std::max)(1u, (std::min)(8u, std::thread::hardware_concurrency() == 0 ? 1u : std::thread::hardware_concurrency()))),
      negotiated_parallel_streams_(1),
      server_allows_auto_create_directories_(false),
      cancel_requested_(false),
      server_port_(0),
      bandwidth_limiter_(std::make_shared<common::BandwidthLimiter>()) {
    chunk_size_manager_.set_limits(config_.internal.initial_chunk_size, config_.internal.min_chunk_size, config_.internal.max_chunk_size);
    chunk_size_manager_.set_adaptation_parameters(
        config_.internal.chunk_size_increase_factor,
        config_.internal.chunk_size_decrease_factor,
        0.3);
    bandwidth_limiter_->set_limit_percent(config_.max_bandwidth_percent);
}

Client::~Client() {
    disconnect();
}

void Client::load_config(const std::string& config_file) {
    config_ = config::ClientConfig::load_from_file(config_file);

    auto& logger = logging::Logger::instance();
    logger.set_level(logging::Logger::string_to_level(config_.logging.level));
    logger.set_console_level(logging::Logger::string_to_level(config_.console.level));
    logger.set_console_output(config_.console.enable);
    logger.set_json_format(config_.logging.format == config::defaults::kLogFormatJson);
    logger.set_file_output(config_.logging.enable ? config_.logging.file : "");

    chunk_size_manager_.set_limits(config_.internal.initial_chunk_size, config_.internal.min_chunk_size, config_.internal.max_chunk_size);
    chunk_size_manager_.set_adaptation_parameters(
        config_.internal.chunk_size_increase_factor,
        config_.internal.chunk_size_decrease_factor,
        0.3);
    negotiated_max_chunk_size_ = config_.internal.max_chunk_size;
    server_allows_auto_create_directories_ = false;
    cancel_requested_ = false;
    bandwidth_limiter_->set_limit_percent(config_.max_bandwidth_percent);
}

void Client::set_config(const config::ClientConfig& config) {
    config_ = config;
    chunk_size_manager_.set_limits(config_.internal.initial_chunk_size, config_.internal.min_chunk_size, config_.internal.max_chunk_size);
    chunk_size_manager_.set_adaptation_parameters(
        config_.internal.chunk_size_increase_factor,
        config_.internal.chunk_size_decrease_factor,
        0.3);
    bandwidth_limiter_->set_limit_percent(config_.max_bandwidth_percent);
}

const config::ClientConfig& Client::get_config() const {
    return config_;
}

void Client::connect(const std::string& server_address, uint16_t port) {
    try {
        event_loop_ = std::make_shared<network::EventLoop>(2);
        event_loop_->start();

        async_socket_ = std::make_shared<network::AsyncSocket>(*event_loop_);
        
        auto connect_promise = std::make_shared<std::promise<asio::error_code>>();
        auto connect_future = connect_promise->get_future();
        async_socket_->connect(server_address, port, [connect_promise](const asio::error_code& ec) {
            connect_promise->set_value(ec);
        });
        
        asio::error_code ec = connect_future.get();
        if (ec) {
            throw std::runtime_error("Connect failed: " + ec.message());
        }

        async_socket_->native_socket().set_option(asio::ip::tcp::no_delay(true));
        if (config_.socket_buffer_size > 0) {
            asio::socket_base::send_buffer_size option_send(static_cast<int>(config_.socket_buffer_size));
            asio::socket_base::receive_buffer_size option_recv(static_cast<int>(config_.socket_buffer_size));
            async_socket_->native_socket().set_option(option_send);
            async_socket_->native_socket().set_option(option_recv);
        }

        if (config_.tls.enable) {
            ssl_ctx_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
            if (!config_.tls.trusted_chain_file.empty()) {
                ssl_ctx_->load_verify_file(config_.tls.trusted_chain_file);
            } else {
                ssl_ctx_->set_default_verify_paths();
            }
            if (config_.tls.server_cert_validation) {
                ssl_ctx_->set_verify_mode(asio::ssl::verify_peer);
            } else {
                ssl_ctx_->set_verify_mode(asio::ssl::verify_none);
            }
            async_socket_->enable_tls(*ssl_ctx_);
            
            auto handshake_promise = std::make_shared<std::promise<asio::error_code>>();
            auto handshake_future = handshake_promise->get_future();
            async_socket_->async_handshake(asio::ssl::stream_base::client, [handshake_promise](const asio::error_code& ec) {
                handshake_promise->set_value(ec);
            });
            
            asio::error_code hec = handshake_future.get();
            if (hec) {
                throw std::runtime_error("TLS handshake failed: " + hec.message());
            }
        }

        server_address_ = server_address;
        server_port_ = port;
        perform_handshake();

        connected_ = true;
        clear_error();
    } catch (const std::exception& e) {
        connected_ = false;
        set_error(e.what());
        if (async_socket_) {
            async_socket_->disconnect();
        }
        async_socket_.reset();
        ssl_ctx_.reset();
        if (event_loop_) {
            event_loop_->stop();
        }
        event_loop_.reset();
        throw;
    }
}

void Client::connect_via_relay(const std::string& relay_address, const std::string& token) {
    try {
        std::string host;
        uint16_t port = 1245;
        size_t colon = relay_address.find(':');
        if (colon != std::string::npos) {
            host = relay_address.substr(0, colon);
            port = static_cast<uint16_t>(std::stoi(relay_address.substr(colon + 1)));
        } else {
            host = relay_address;
        }

        event_loop_ = std::make_shared<network::EventLoop>(2);
        event_loop_->start();

        async_socket_ = std::make_shared<network::AsyncSocket>(*event_loop_);
        
        auto connect_promise = std::make_shared<std::promise<asio::error_code>>();
        auto connect_future = connect_promise->get_future();
        async_socket_->connect(host, port, [connect_promise](const asio::error_code& ec) {
            connect_promise->set_value(ec);
        });
        
        asio::error_code ec = connect_future.get();
        if (ec) {
            throw std::runtime_error("Failed to connect to relay: " + ec.message());
        }

        async_socket_->native_socket().set_option(asio::ip::tcp::no_delay(true));
        if (config_.socket_buffer_size > 0) {
            asio::socket_base::send_buffer_size option_send(static_cast<int>(config_.socket_buffer_size));
            asio::socket_base::receive_buffer_size option_recv(static_cast<int>(config_.socket_buffer_size));
            async_socket_->native_socket().set_option(option_send);
            async_socket_->native_socket().set_option(option_recv);
        }

        // Send RELAY_CONNECT command
        std::string conn_cmd = "RELAY_CONNECT " + token + "\n";
        execute_io_sync([&](auto&& handler) {
            async_socket_->async_write(conn_cmd.data(), conn_cmd.size(), std::move(handler));
        });

        if (config_.tls.enable) {
            ssl_ctx_ = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
            if (!config_.tls.trusted_chain_file.empty()) {
                ssl_ctx_->load_verify_file(config_.tls.trusted_chain_file);
            } else {
                ssl_ctx_->set_default_verify_paths();
            }
            if (config_.tls.server_cert_validation) {
                ssl_ctx_->set_verify_mode(asio::ssl::verify_peer);
            } else {
                ssl_ctx_->set_verify_mode(asio::ssl::verify_none);
            }
            async_socket_->enable_tls(*ssl_ctx_);
            
            auto handshake_promise = std::make_shared<std::promise<asio::error_code>>();
            auto handshake_future = handshake_promise->get_future();
            async_socket_->async_handshake(asio::ssl::stream_base::client, [handshake_promise](const asio::error_code& ec) {
                handshake_promise->set_value(ec);
            });
            
            asio::error_code hec = handshake_future.get();
            if (hec) {
                throw std::runtime_error("TLS handshake failed over relay: " + hec.message());
            }
        }

        server_address_ = host;
        server_port_ = port;
        perform_handshake();

        connected_ = true;
        clear_error();
    } catch (const std::exception& e) {
        connected_ = false;
        set_error(e.what());
        if (async_socket_) {
            async_socket_->disconnect();
        }
        async_socket_.reset();
        ssl_ctx_.reset();
        if (event_loop_) {
            event_loop_->stop();
        }
        event_loop_.reset();
        throw;
    }
}

void Client::disconnect() {
    if (connected_ && async_socket_) {
        try {
            protocol::Disconnect msg;
            send_message(msg);
        } catch (...) {
            // Ignore errors during disconnect message sending
        }
    }
    if (async_socket_) {
        async_socket_->disconnect();
    }
    async_socket_.reset();
    ssl_ctx_.reset();
    if (event_loop_) {
        event_loop_->stop();
    }
    event_loop_.reset();
    connected_ = false;
}

bool Client::is_connected() const {
    return connected_ && async_socket_ && async_socket_->is_open();
}

void Client::set_security_level(crypto::SecurityLevel level) {
    security_level_ = level;
}

void Client::transfer_file(const std::string& local_path, const std::string& remote_path, bool resume) {
    if (!connected_) {
        throw NetworkException("Client is not connected");
    }
    if (!file::FileManager::is_regular_file(local_path)) {
        throw FileException("Source is not a regular file: " + local_path);
    }

    try {
        uint64_t file_size = file::FileManager::file_size(local_path);
        transfer_single_file(local_path, remote_path, resume);
        trigger_webhook("upload", local_path, remote_path, "success", file_size);
    } catch (const std::exception& e) {
        trigger_webhook("upload", local_path, remote_path, "failed", 0, e.what());
        throw;
    }
}

void Client::transfer_directory(const std::string& local_path,
                                const std::string& remote_path,
                                bool recursive,
                                bool resume) {
    if (!connected_) {
        throw NetworkException("Client is not connected");
    }
    if (!file::FileManager::is_directory(local_path)) {
        throw FileException("Source is not a directory: " + local_path);
    }

    uint64_t total_bytes = 0;
    uint32_t files_transferred = 0;

    try {
        auto files = file::FileManager::list_directory(local_path, recursive);
        
        struct FileTransferTask {
            std::string local_path;
            std::string remote_path;
        };
        std::vector<FileTransferTask> file_tasks;
        
        // Normalize local_path using filesystem path
        std::filesystem::path p_local = std::filesystem::u8path(local_path).lexically_normal();
        std::string norm_local = p_local.u8string();
        while (!norm_local.empty() && (norm_local.back() == '/' || norm_local.back() == '\\')) {
            norm_local.pop_back();
        }
        std::string norm_local_cmp = norm_local;
#ifdef _WIN32
        // Windows is case-insensitive
        std::transform(norm_local_cmp.begin(), norm_local_cmp.end(), norm_local_cmp.begin(), ::tolower);
#endif

        std::vector<std::pair<std::string, uint64_t>> files_to_report;

        for (const auto& entry : files) {
            std::filesystem::path p_entry = std::filesystem::u8path(entry.path).lexically_normal();
            std::string norm_entry = p_entry.u8string();
            std::string norm_entry_cmp = norm_entry;
#ifdef _WIN32
            std::transform(norm_entry_cmp.begin(), norm_entry_cmp.end(), norm_entry_cmp.begin(), ::tolower);
#endif

            std::string relative = entry.path;
            if (norm_entry_cmp.rfind(norm_local_cmp, 0) == 0) {
                relative = norm_entry.substr(norm_local.length());
            }
            while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
                relative.erase(relative.begin());
            }

            std::string destination = file::FileManager::join_path(remote_path, common::convert_to_unix_path(relative));
            if (entry.is_directory) {
                if (config_.create_empty_directories) {
                    if (!server_allows_auto_create_directories_) {
                        throw FileException("Server policy does not allow empty directory creation: " + destination);
                    }
                    create_empty_directory(destination);
                }
            } else {
                file_tasks.push_back({entry.path, destination});
                files_to_report.push_back({entry.path, entry.size});
                total_bytes += entry.size;
                files_transferred++;
            }
        }
        
        if (file_list_callback_) {
            file_list_callback_(files_to_report);
        }
        
        if (file_tasks.empty()) {
            trigger_webhook("upload", local_path, remote_path, "success", 0, "", 0);
            return;
        }
        
        uint32_t max_threads = negotiated_parallel_streams_ == 0 ? 1 : negotiated_parallel_streams_;
        if (max_threads <= 1 || file_tasks.size() <= 1) {
            // Sequential transfer
            for (const auto& task : file_tasks) {
                try {
                    transfer_single_file(task.local_path, task.remote_path, resume);
                } catch (const FileSkippedException& e) {
                    LOG_INFO("File skipped: " + task.local_path);
                    disconnect();
                    connect(server_address_, server_port_);
                }
            }
            trigger_webhook("upload", local_path, remote_path, "success", total_bytes, "", files_transferred);
            return;
        }
        
        // Concurrent transfer over multiple socket connections
        std::mutex queue_mutex;
        size_t next_task_idx = 0;
        std::exception_ptr first_error;
        std::mutex error_mutex;
        
        auto record_error = [&](std::exception_ptr error) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!first_error) {
                first_error = error;
            }
            request_cancel();
        };
        
        uint32_t num_threads = (std::min)(max_threads, static_cast<uint32_t>(file_tasks.size()));
        std::mutex progress_callback_mutex;
        auto safe_progress_callback = [&](uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file) {
            if (progress_callback_) {
                std::lock_guard<std::mutex> lock(progress_callback_mutex);
                progress_callback_(bytes_transferred, total_bytes, current_file);
            }
        };
        
        auto worker_body = [&]() {
            try {
                Client stream_client;
                stream_client.set_config(config_);
                stream_client.set_security_level(security_level_);
                stream_client.set_requested_parallel_streams(1); // Enforce single thread/connection per file transfer
                stream_client.bandwidth_limiter_ = bandwidth_limiter_;
                stream_client.set_progress_callback(safe_progress_callback); // Propagate progress callback safely
                stream_client.set_overwrite_callback(overwrite_callback_);
                stream_client.parent_client_ = this;
                
                {
                    std::lock_guard<std::mutex> lock(workers_mutex_);
                    if (cancel_requested_) {
                        return;
                    }
                    active_workers_.push_back(&stream_client);
                }
                
                struct Cleanup {
                    Client* client;
                    std::mutex& mutex;
                    std::vector<Client*>& workers;
                    ~Cleanup() {
                        std::lock_guard<std::mutex> lock(mutex);
                        auto it = std::find(workers.begin(), workers.end(), client);
                        if (it != workers.end()) {
                            workers.erase(it);
                        }
                    }
                } cleanup{&stream_client, workers_mutex_, active_workers_};

                stream_client.connect(server_address_, server_port_);
                
                while (true) {
                    if (cancel_requested_ || stream_client.cancel_requested_) {
                        break;
                    }
                    size_t idx;
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex);
                        if (next_task_idx >= file_tasks.size() || first_error) {
                            break;
                        }
                        idx = next_task_idx++;
                    }
                    
                    const auto& task = file_tasks[idx];
                    try {
                        stream_client.transfer_single_file(task.local_path, task.remote_path, resume);
                    } catch (const FileSkippedException& e) {
                        LOG_INFO("File skipped: " + task.local_path);
                        stream_client.disconnect();
                        stream_client.connect(server_address_, server_port_);
                    }
                }
            } catch (...) {
                record_error(std::current_exception());
            }
        };
        
        std::vector<std::thread> workers;
        workers.reserve(num_threads);
        try {
            for (uint32_t i = 0; i < num_threads; ++i) {
                workers.emplace_back(worker_body);
            }
        } catch (...) {
            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            throw;
        }
        
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        if (first_error) {
            std::rethrow_exception(first_error);
        }
        trigger_webhook("upload", local_path, remote_path, "success", total_bytes, "", files_transferred);
    } catch (const std::exception& e) {
        trigger_webhook("upload", local_path, remote_path, "failed", total_bytes, e.what(), files_transferred);
        throw;
    }
}

void Client::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

void Client::set_overwrite_callback(OverwriteCallback callback) {
    overwrite_callback_ = std::move(callback);
}

static std::string normalize_control_path(const std::string& path) {
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    for (auto& c : norm) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    return norm;
}

void Client::set_file_list_callback(FileListCallback callback) {
    file_list_callback_ = std::move(callback);
}

void Client::pause_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    paused_files_[normalize_control_path(path)] = true;
    LOG_INFO("Paused file: " + path);
}

void Client::resume_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    paused_files_[normalize_control_path(path)] = false;
    LOG_INFO("Resumed file: " + path);
}

void Client::skip_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(control_mutex_);
    skipped_files_[normalize_control_path(path)] = true;
    LOG_INFO("Skipped file: " + path);
}

bool Client::is_file_paused(const std::string& path) {
    if (parent_client_) {
        return parent_client_->is_file_paused(path);
    }
    std::lock_guard<std::mutex> lock(control_mutex_);
    auto it = paused_files_.find(normalize_control_path(path));
    return (it != paused_files_.end()) && it->second;
}

bool Client::is_file_skipped(const std::string& path) {
    if (parent_client_) {
        return parent_client_->is_file_skipped(path);
    }
    std::lock_guard<std::mutex> lock(control_mutex_);
    auto it = skipped_files_.find(normalize_control_path(path));
    return (it != skipped_files_.end()) && it->second;
}

protocol::TransferStatusResponse Client::query_transfer_status(const std::string& session_id) {
    if (!connected_) {
        throw ProtocolException("Client is not connected");
    }
    
    protocol::TransferStatusRequest request;
    request.session_id = session_id;
    send_message(request);
    
    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::TransferStatusResponse*>(response_msg.get());
    if (!response) {
        throw ProtocolException("Invalid response to transfer status request");
    }
    
    return *response;
}

std::string Client::get_last_error() const {
    return last_error_;
}

void Client::perform_handshake() {
    protocol::HandshakeRequest request;
    request.client_version = common::get_version_string();
    request.client_nonce = common::generate_random_bytes(16);
    request.security_level = security_level_;
    request.max_chunk_size = chunk_size_manager_.get_max_chunk_size();
    request.file_size = 0;
    request.requested_parallel_streams = requested_parallel_streams_;
    // Set auth fields
    request.username = config_.internal.username;
    if (config_.internal.auth_method == "password")      request.auth_method_id = 1;
    else if (config_.internal.auth_method == "mlkem")    request.auth_method_id = 2;
    else                                         request.auth_method_id = 0;

    send_message(request);

    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::HandshakeResponse*>(response_msg.get());
    if (!response || !response->authentication_required && response->server_version.empty()) {
        throw ProtocolException("Invalid handshake response");
    }

    negotiated_security_level_ = response->accepted_security_level;
    if (negotiated_security_level_ != security_level_) {
        std::string req_name = (security_level_ == crypto::SecurityLevel::HIGH) ? "HIGH" :
                               (security_level_ == crypto::SecurityLevel::FAST) ? "FAST" :
                               (security_level_ == crypto::SecurityLevel::AES) ? "AES" : "AES_256_GCM";
        std::string neg_name = (negotiated_security_level_ == crypto::SecurityLevel::HIGH) ? "HIGH" :
                               (negotiated_security_level_ == crypto::SecurityLevel::FAST) ? "FAST" :
                               (negotiated_security_level_ == crypto::SecurityLevel::AES) ? "AES" : "AES_256_GCM";
        throw CryptoException("Security level mismatch: client requested " + req_name + 
                              " but server negotiated " + neg_name);
    }
    negotiated_max_chunk_size_ = response->max_chunk_size == 0
        ? config_.internal.max_chunk_size
        : (std::min)(config_.internal.max_chunk_size, static_cast<size_t>(response->max_chunk_size));
    negotiated_parallel_streams_ = response->accepted_parallel_streams == 0 ? 1 : response->accepted_parallel_streams;
    server_allows_auto_create_directories_ = response->auto_create_directories_allowed;
    chunk_size_manager_.set_max_chunk_size(negotiated_max_chunk_size_);
    if (response->authentication_required) {
        if (config_.internal.secret_key.empty()) {
            throw CryptoException("Server requires authentication but no secret key is configured");
        }
        crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, config_.internal.secret_key);
        
        // Derive dynamic session key with nonces
        auto derived = common::derive_session_key(
            config_.internal.secret_key,
            {},
            response->server_nonce,
            request.client_nonce);
        std::string hex_derived = common::to_hex_string(derived);
        crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, "0x" + hex_derived);
        LOG_DEBUG("Derived dynamic session key with nonces");
    }

    // User authentication phase
    if (!config_.internal.username.empty() && config_.internal.auth_method != "none" && request.auth_method_id != 0) {
        auto challenge_msg = receive_message();
        auto* auth_challenge = dynamic_cast<protocol::AuthChallenge*>(challenge_msg.get());
        if (!auth_challenge) {
            throw ProtocolException("Expected AuthChallenge from server");
        }

        std::vector<uint8_t> proof;
        std::vector<uint8_t> mlkem_shared_secret;

        if (auth_challenge->method == 1) { // PASSWORD
            // Derive key from password
            auto salt = crypto::hex_to_bytes(auth_challenge->salt_hex);
            auto dk = crypto::pbkdf2_sha3_256(config_.internal.password, salt,
                                               auth_challenge->pbkdf2_iterations, 32);
            // proof = SHA3-256(dk || challenge_nonce)
            std::vector<uint8_t> preimage = dk;
            preimage.insert(preimage.end(),
                            auth_challenge->challenge_nonce.begin(),
                            auth_challenge->challenge_nonce.end());
            proof = crypto::sha3_256(preimage);

        } else if (auth_challenge->method == 2) { // ML-KEM
            if (config_.internal.private_key_file.empty()) {
                throw CryptoException("ML-KEM private key file not configured");
            }
            crypto::MlKemLevel level;
            auto privkey = crypto::load_private_key(config_.internal.private_key_file, level,
                                                     config_.internal.private_key_passphrase);
            auto shared_secret = crypto::MlKem::decapsulate(privkey,
                                                             auth_challenge->kem_ciphertext,
                                                             level);
            mlkem_shared_secret = shared_secret;
            // proof = SHA3-256(shared_secret || kem_nonce)
            std::vector<uint8_t> preimage = shared_secret;
            preimage.insert(preimage.end(),
                            auth_challenge->kem_nonce.begin(),
                            auth_challenge->kem_nonce.end());
            proof = crypto::sha3_256(preimage);
        }

        protocol::AuthResponse auth_resp;
        auth_resp.proof = proof;
        send_message(auth_resp);

        auto result_msg = receive_message();
        auto* auth_result = dynamic_cast<protocol::AuthResult*>(result_msg.get());
        if (!auth_result || !auth_result->success) {
            std::string err = auth_result ? auth_result->error_message : "No auth result";
            throw AuthException("Authentication failed: " + err);
        }
        LOG_INFO("Authenticated as '" + config_.internal.username + "'");

        if (auth_challenge->method == 2 && crypto_engine_) {
            auto derived = common::derive_session_key(
                config_.internal.secret_key,
                mlkem_shared_secret,
                response->server_nonce,
                request.client_nonce);
            std::string hex_derived = common::to_hex_string(derived);
            crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, "0x" + hex_derived);
            LOG_DEBUG("Re-derived dynamic session key with ML-KEM shared secret");
        }
    }
}

void Client::send_message(const protocol::Message& message) {
    if (!async_socket_) {
        throw NetworkException("Socket is not connected");
    }

    auto data = message.serialize();
    if (crypto_engine_) {
        data = encrypt_message(data);
    }
    if (data.size() > config::defaults::kMaxFrameSize) {
        throw ProtocolException("Client attempted to send a message exceeding the 64MB frame limit: " + std::to_string(data.size()) + " bytes");
    }

    uint32_t length = htonl(static_cast<uint32_t>(data.size()));
    execute_io_sync([&](auto&& handler) {
        std::vector<asio::const_buffer> buffers;
        buffers.reserve(2);
        buffers.push_back(asio::buffer(&length, sizeof(length)));
        if (!data.empty()) {
            buffers.push_back(asio::buffer(data.data(), data.size()));
        }
        async_socket_->async_write(buffers, std::move(handler));
    });
}

std::unique_ptr<protocol::Message> Client::receive_message() {
    if (!async_socket_) {
        throw NetworkException("Socket is not connected");
    }

    uint32_t length_net = 0;
    execute_io_sync([&](auto&& handler) {
        async_socket_->async_read(&length_net, sizeof(length_net), std::move(handler));
    });
    uint32_t length = ntohl(length_net);

    // Safety check on length to avoid bad_alloc crash
    if (length > config::defaults::kMaxFrameSize) {
        throw ProtocolException("Client received a message with length exceeding the 64MB limit: " + std::to_string(length) + " bytes");
    }

    std::vector<uint8_t> data(length);
    size_t total_received = 0;
    while (total_received < data.size()) {
        size_t received = execute_io_sync([&](auto&& handler) {
            async_socket_->async_read(data.data() + total_received, data.size() - total_received, std::move(handler));
        });
        if (received == 0) {
            throw NetworkException("Failed to receive message data (0 bytes received)");
        }
        total_received += received;
    }

    if (crypto_engine_) {
        data = decrypt_message(data);
    }

    return protocol::Message::deserialize(data);
}

std::vector<uint8_t> Client::encrypt_message(const std::vector<uint8_t>& data) {
    if (!crypto_engine_) {
        return data;
    }
    return crypto_engine_->encrypt(data);
}

std::vector<uint8_t> Client::decrypt_message(const std::vector<uint8_t>& data) {
    if (!crypto_engine_) {
        return data;
    }
    return crypto_engine_->decrypt(data);
}

void Client::transfer_single_file(const std::string& local_path, const std::string& remote_path, bool resume) {
    cancel_requested_ = false;
    bool is_sym = file::FileManager::is_symlink(local_path);
    uint64_t total_size = is_sym ? 0 : file::FileManager::file_size(local_path);

    uint64_t resume_offset = 0;
    uint64_t remote_file_size = 0;
    // Probe the destination without arming truncation. Delta sync needs the
    // existing remote file as its basis, and overwrite mode re-arms truncation
    // after the overwrite decision is known.
    send_file_request(local_path, remote_path, resume, false, resume_offset, &remote_file_size);
    if (resume_offset > total_size) {
        throw FileException("Resume offset is larger than source file");
    }

    bool do_delta_sync = true;
    if (!is_sym && remote_file_size > 0 && !resume) {
        if (overwrite_callback_) {
            auto decision = overwrite_callback_(remote_path, remote_file_size);
            if (decision == OverwriteDecision::SKIP) {
                LOG_INFO("Skipping existing file: " + remote_path);
                if (progress_callback_) {
                    progress_callback_(total_size, total_size, local_path);
                }
                return;
            } else if (decision == OverwriteDecision::CANCEL) {
                request_cancel();
                throw FileException("Transfer cancelled by user");
            } else if (decision == OverwriteDecision::OVERWRITE) {
                do_delta_sync = false;
            } else if (decision == OverwriteDecision::DELTA_SYNC) {
                do_delta_sync = true;
            }
        }
        
        if (do_delta_sync) {
            LOG_INFO("Remote file exists (size " + std::to_string(remote_file_size) + " bytes). Performing delta sync for " + local_path);
            auto should_cancel = [&]() {
                return cancel_requested_.load();
            };
        
        // 1. Send BlockHashesRequest with adaptive block size
        protocol::BlockHashesRequest req;
        req.file_path = remote_path;
        req.block_size = file::FileManager::compute_optimal_block_size(total_size);
        
        send_message(req);
        
        // 2. Receive BlockHashesResponse
        auto resp_msg = receive_message();
        auto resp = dynamic_cast<protocol::BlockHashesResponse*>(resp_msg.get());
        if (!resp) {
            throw ProtocolException("Expected BlockHashesResponse");
        }
        if (!resp->success) {
            throw FileException("Failed to get remote block hashes: " + resp->error_message);
        }
        
        // 3. Compute local block hashes (with progress feedback for large files)
        uint64_t block_size = resp->block_size > 0 ? resp->block_size : 65536;
        std::vector<uint8_t> local_full_hash;
        if (total_size >= 64ull * 1024 * 1024) {
            LOG_INFO("Computing local block signatures for " + local_path + " (block size " + std::to_string(block_size) + " bytes)...");
        }
        auto local_hashes = file::FileManager::compute_block_hashes(local_path, block_size, should_cancel, &local_full_hash);
        if (total_size >= 64ull * 1024 * 1024) {
            LOG_INFO("Local block signatures computed (" + std::to_string(local_hashes.size()) + " blocks).");
        }
        
        // 4. Compare hashes block by block
        struct BlockRange {
            uint64_t offset;
            uint64_t size;
        };
        std::vector<BlockRange> diffs;
        const auto& remote_blocks = resp->blocks;
        
        size_t i = 0;
        for (; i < (std::min)(local_hashes.size(), remote_blocks.size()); ++i) {
            if (local_hashes[i].hash != remote_blocks[i].hash) {
                diffs.push_back({local_hashes[i].offset, (std::min)(block_size, total_size - local_hashes[i].offset)});
            }
        }
        // If local file is larger, transfer remaining blocks
        for (; i < local_hashes.size(); ++i) {
            diffs.push_back({local_hashes[i].offset, (std::min)(block_size, total_size - local_hashes[i].offset)});
        }
        
        // 5. Merge contiguous differing blocks to optimize transfers
        std::vector<BlockRange> merged_diffs;
        for (const auto& diff : diffs) {
            if (!merged_diffs.empty() && merged_diffs.back().offset + merged_diffs.back().size == diff.offset) {
                merged_diffs.back().size += diff.size;
            } else {
                merged_diffs.push_back(diff);
            }
        }
        
        // 6. Transfer differing blocks
        bandwidth_monitor_.reset();
        chunk_size_manager_.reset();
        common::BandwidthMonitor transfer_monitor;
        
        auto make_chunk_manager = [&]() {
            return common::ChunkSizeManager(
                config_.internal.initial_chunk_size,
                config_.internal.min_chunk_size,
                negotiated_max_chunk_size_,
                config_.internal.chunk_size_increase_factor,
                config_.internal.chunk_size_decrease_factor,
                0.3);
        };
        
        auto chunk_manager = make_chunk_manager();
        
        // Setup progress tracking: we want to track overall progress based on the total file size
        uint64_t total_diff_bytes = 0;
        for (const auto& diff : merged_diffs) {
            total_diff_bytes += diff.size;
        }
        uint64_t matching_bytes = total_size > total_diff_bytes ? total_size - total_diff_bytes : 0;
        std::atomic<uint64_t> total_transferred(matching_bytes);
        
        auto progress_callback_lambda = [&](uint64_t delta) {
            uint64_t current = total_transferred.fetch_add(delta) + delta;
            if (progress_callback_) {
                progress_callback_(current, total_size, local_path);
            }
        };
        
        if (merged_diffs.empty()) {
            // No differing blocks! File is already identical, but we must send a 0-byte last chunk to finalize the transfer
            protocol::FileData data_msg;
            data_msg.offset = total_size;
            data_msg.uncompressed_size = 0;
            data_msg.is_last_chunk = true;
            data_msg.compressed = false;
            send_message(data_msg);
            
            auto ack_msg = receive_message();
            auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
            if (!ack || !ack->success) {
                throw FileException("Transfer finalization failed: " + (ack ? ack->error_message : "No acknowledgment"));
            }
            if (progress_callback_) {
                progress_callback_(total_size, total_size, local_path);
            }
        } else {
            // Stream each merged diff range
            for (size_t d = 0; d < merged_diffs.size(); ++d) {
                const auto& diff = merged_diffs[d];
                bool is_last_diff = (d == merged_diffs.size() - 1);
                
                send_file_range(
                    local_path,
                    diff.offset,
                    diff.offset + diff.size,
                    total_size,
                    chunk_manager,
                    transfer_monitor,
                    progress_callback_lambda,
                    is_last_diff
                );
            }
        }
        
        // E2E Integrity verification
        if (total_size > 0 && !merged_diffs.empty()) {
            LOG_INFO("Performing E2E integrity check for: " + local_path);
            auto local_hash = local_full_hash.empty()
                ? file::FileManager::compute_file_hash(local_path, should_cancel)
                : local_full_hash;
            
            protocol::FileVerifyRequest verify_req;
            verify_req.file_path = remote_path;
            verify_req.expected_hash = local_hash;
            
            send_message(verify_req);
            
            auto verify_resp_msg = receive_message();
            auto verify_resp = dynamic_cast<protocol::FileVerifyResponse*>(verify_resp_msg.get());
            if (!verify_resp) {
                throw ProtocolException("Expected FileVerifyResponse");
            }
            
            if (!verify_resp->success) {
                throw FileException("Integrity verification failed for " + local_path + ": " + verify_resp->error_message);
            }
            
            LOG_INFO("E2E Integrity verification succeeded for: " + local_path);
        }
        
        return; // Done with delta sync!
        }
    }

    if (!is_sym && remote_file_size > 0 && !resume && !do_delta_sync) {
        send_file_request(local_path, remote_path, false, true, resume_offset, &remote_file_size);
        resume_offset = 0;
    }

    bandwidth_monitor_.reset();
    chunk_size_manager_.reset();

    uint32_t stream_count = resume ? 1 : choose_parallel_stream_count(total_size - resume_offset);
    
    // Initialize BufferPool for memory reuse across all streams
    buffer_pool_ = std::make_shared<BufferPool>(negotiated_max_chunk_size_, stream_count * 4);
    
    common::BandwidthMonitor transfer_monitor;
    auto make_chunk_manager = [&]() {
        return common::ChunkSizeManager(
            config_.internal.initial_chunk_size,
            config_.internal.min_chunk_size,
            negotiated_max_chunk_size_,
            config_.internal.chunk_size_increase_factor,
            config_.internal.chunk_size_decrease_factor,
            0.3);
    };
    if (stream_count <= 1 || total_size == resume_offset) {
        send_file_data(local_path, resume_offset, total_size);
        if (total_size == resume_offset && progress_callback_) {
            progress_callback_(total_size, total_size, local_path);
        }
        return;
    }

    uint64_t total_to_transfer = total_size - resume_offset;
    uint64_t partition_size = (total_to_transfer + stream_count - 1) / stream_count;

    std::atomic<uint64_t> transferred(resume_offset);
    std::exception_ptr first_error;
    std::mutex error_mutex;
    std::mutex progress_mutex;

    auto record_error = [&](std::exception_ptr error) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!first_error) {
            first_error = error;
        }
        request_cancel();
    };

    auto progress_callback_lambda = [&](uint64_t delta) {
        uint64_t current = transferred.fetch_add(delta) + delta;
        if (progress_callback_) {
            std::lock_guard<std::mutex> lock(progress_mutex);
            progress_callback_(current, total_size, local_path);
        }
    };

    auto worker_body = [&](uint32_t stream_index) {
        try {
            Client stream_client;
            stream_client.set_config(config_);
            stream_client.set_security_level(security_level_);
            stream_client.set_requested_parallel_streams(1);
            stream_client.set_buffer_pool(buffer_pool_); // Propagate buffer pool
            stream_client.bandwidth_limiter_ = bandwidth_limiter_;
            stream_client.parent_client_ = this;
            
            {
                std::lock_guard<std::mutex> lock(workers_mutex_);
                if (cancel_requested_) {
                    return;
                }
                active_workers_.push_back(&stream_client);
            }

            struct Cleanup {
                Client* client;
                std::mutex& mutex;
                std::vector<Client*>& workers;
                ~Cleanup() {
                    std::lock_guard<std::mutex> lock(mutex);
                    auto it = std::find(workers.begin(), workers.end(), client);
                    if (it != workers.end()) {
                        workers.erase(it);
                    }
                }
            } cleanup{&stream_client, workers_mutex_, active_workers_};

            stream_client.connect(server_address_, server_port_);
            common::ChunkSizeManager worker_chunk_manager = make_chunk_manager();

            uint64_t worker_resume_offset = 0;
            stream_client.send_file_request(local_path, remote_path, false, false, worker_resume_offset);

            uint64_t start = resume_offset + stream_index * partition_size;
            if (start < total_size) {
                uint64_t end = (std::min)(start + partition_size, total_size);
                stream_client.send_file_range(local_path,
                                               start,
                                               end,
                                               total_size,
                                               worker_chunk_manager,
                                               transfer_monitor,
                                               progress_callback_lambda,
                                               false);
            }
        } catch (...) {
            record_error(std::current_exception());
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(stream_count - 1);
    try {
        for (uint32_t i = 1; i < stream_count; ++i) {
            workers.emplace_back(worker_body, i);
        }
    } catch (...) {
        record_error(std::current_exception());
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    try {
        common::ChunkSizeManager main_chunk_manager = make_chunk_manager();
        uint64_t start = resume_offset;
        if (start < total_size) {
            uint64_t end = (std::min)(start + partition_size, total_size);
            send_file_range(local_path,
                            start,
                            end,
                            total_size,
                            main_chunk_manager,
                            transfer_monitor,
                            progress_callback_lambda,
                            false);
        }
    } catch (...) {
        record_error(std::current_exception());
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    if (first_error) {
        std::rethrow_exception(first_error);
    }

    if (cancel_requested_) {
        throw FileException("Transfer cancelled");
    }

    if (transferred.load() != total_size) {
        throw FileException("Parallel transfer ended before all bytes were acknowledged");
    }

    // Send finalization chunk on the main connection
    protocol::FileData final_msg;
    final_msg.offset = total_size;
    final_msg.uncompressed_size = 0;
    final_msg.is_last_chunk = true;
    final_msg.compressed = false;
    send_message(final_msg);

    auto ack_msg = receive_message();
    auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
    if (!ack || !ack->success) {
        throw FileException("Transfer finalization failed: " + (ack ? ack->error_message : "No acknowledgment"));
    }
    
    // E2E Integrity check
    if (total_size > 0) {
        LOG_INFO("Performing E2E integrity check for: " + local_path);
        auto local_hash = file::FileManager::compute_file_hash(local_path, [&]() {
            return cancel_requested_.load();
        });
        
        protocol::FileVerifyRequest verify_req;
        verify_req.file_path = remote_path;
        verify_req.expected_hash = local_hash;
        
        send_message(verify_req);
        
        auto verify_resp_msg = receive_message();
        auto verify_resp = dynamic_cast<protocol::FileVerifyResponse*>(verify_resp_msg.get());
        if (!verify_resp) {
            throw ProtocolException("Expected FileVerifyResponse");
        }
        
        if (!verify_resp->success) {
            throw FileException("Integrity verification failed for " + local_path + ": " + verify_resp->error_message);
        }
        
        LOG_INFO("E2E Integrity verification succeeded for: " + local_path);
    }
}

void Client::send_file_request(const std::string& local_path,
                               const std::string& remote_path,
                               bool resume,
                               bool truncate_destination,
                               uint64_t& resume_offset,
                               uint64_t* remote_file_size) {
    protocol::FileRequest request;
    request.source_path = common::convert_to_unix_path(local_path);
    request.destination_path = common::convert_to_unix_path(remote_path);
    request.recursive = false;
    request.resume_offset = resume ? 1 : 0;
    request.auto_create_directories = config_.auto_create_directories;
    request.truncate_destination = truncate_destination;
    
    // Assign metadata fields
    request.permissions = file::FileManager::get_permissions(local_path);
    request.is_symlink = file::FileManager::is_symlink(local_path);
    if (request.is_symlink) {
        request.symlink_target = file::FileManager::read_symlink(local_path);
    }
    request.file_size = request.is_symlink ? 0 : file::FileManager::file_size(local_path);
    
    send_message(request);

    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::FileResponse*>(response_msg.get());
    if (!response) {
        throw ProtocolException("Invalid file response");
    }
    session_id_ = response->session_id;
    if (parent_client_) {
        std::lock_guard<std::mutex> lock(parent_client_->control_mutex_);
        parent_client_->session_id_ = response->session_id;
    }
    if (!response->success) {
        throw FileException(response->error_message);
    }

    resume_offset = resume ? response->resume_offset : 0;
    if (remote_file_size) {
        *remote_file_size = response->file_size;
    }
}

void Client::send_file_data(const std::string& file_path, uint64_t resume_offset, uint64_t total_size) {
    if (total_size == 0 && resume_offset == 0) {
        protocol::FileData data_msg;
        data_msg.offset = 0;
        data_msg.uncompressed_size = 0;
        data_msg.is_last_chunk = true;
        data_msg.compressed = false;
        send_message(data_msg);

        auto ack_msg = receive_message();
        auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
        if (!ack || !ack->success) {
            throw FileException("Transfer failed: " + (ack ? ack->error_message : "No acknowledgment"));
        }
        if (progress_callback_) {
            progress_callback_(0, 0, file_path);
        }
        return;
    }

    send_file_range(file_path, resume_offset, total_size, total_size, chunk_size_manager_, bandwidth_monitor_, [&](uint64_t delta) {
        if (progress_callback_) {
            progress_callback_(resume_offset + bandwidth_monitor_.get_total_bytes(), total_size, file_path);
        }
    }, true);
}

void Client::send_file_range(const std::string& file_path,
                             uint64_t start_offset,
                             uint64_t end_offset,
                             uint64_t total_size,
                             common::ChunkSizeManager& shared_chunk_manager,
                             common::BandwidthMonitor& shared_bandwidth_monitor,
                             const std::function<void(uint64_t)>& progress_delta_callback,
                             bool is_final_range,
                             crypto::Sha3Hasher* stream_hasher) {
    if (!buffer_pool_) {
        // Fallback buffer pool initialization
        buffer_pool_ = std::make_shared<BufferPool>(negotiated_max_chunk_size_ > 0 ? negotiated_max_chunk_size_ : 10 * 1024 * 1024, 16);
    }

    bool compress = common::is_compressible(file_path);

    std::atomic<bool> ack_thread_failed(false);
    std::string ack_thread_error;
    std::mutex ack_mutex;
    std::condition_variable ack_cv;
    
    std::atomic<uint64_t> last_acknowledged_offset(start_offset);
    std::atomic<uint64_t> in_flight_bytes(0);
    
    std::map<uint64_t, size_t> in_flight_chunks;
    
    // Flow-control window: keep up to 64 MB in flight to allow pipelining.
    // The chunk size is capped to at most half this value so the wait
    // condition (in_flight + chunk_size <= window) is always reachable.
    uint64_t configured_window_bytes = normalized_window_bytes(config_.internal.inflight_window_bytes);
    if (config_.internal.tcp_info_window && async_socket_ && !async_socket_->is_tls()) {
        configured_window_bytes = network::windows_experimental::recommended_tcp_inflight_window(
            async_socket_->native_socket().native_handle(),
            configured_window_bytes);
    }
    std::atomic<uint64_t> max_window_bytes(configured_window_bytes);
    const uint64_t max_batch_bytes = normalized_batch_bytes(config_.internal.batch_bytes, configured_window_bytes);
    const size_t max_batch_chunks = normalized_batch_chunks(config_.internal.batch_chunks);
    const size_t max_chunk_for_window = static_cast<size_t>(configured_window_bytes / 2); // 32 MB per chunk max by default

    // Background disk read-ahead thread
    ReadAheadQueue read_queue(4); // Keep at most 4 chunks pre-read in memory
    std::atomic<bool> reader_failed(false);
    std::atomic<bool> is_skipped_error(false);
    std::string reader_error;

    std::thread ack_thread;
    std::thread reader_thread;

    try {
        // Background ACK listener thread
        ack_thread = std::thread([&]() {
            try {
                while (last_acknowledged_offset.load() < end_offset && !cancel_requested_ && !ack_thread_failed.load()) {
                    auto ack_msg = receive_message();
                    auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
                    if (!ack || !ack->success) {
                        throw FileException("Transfer failed: " + (ack ? ack->error_message : "No acknowledgment"));
                    }
                    
                    uint64_t bytes_received = ack->bytes_received;
                    
                    // Collect progress deltas to report OUTSIDE the ack_mutex.
                    // Calling progress_delta_callback while holding ack_mutex
                    // cascades into shared mutexes + slow console I/O, which
                    // serialises all parallel workers and collapses throughput.
                    uint64_t progress_delta = 0;
                    
                    std::lock_guard<std::mutex> lock(ack_mutex);
                    auto it = in_flight_chunks.begin();
                    while (it != in_flight_chunks.end() && it->first + it->second <= bytes_received) {
                        uint64_t chunk_offset = it->first;
                        size_t chunk_size = it->second;
                        
                        shared_bandwidth_monitor.record_bytes(chunk_size);
                        shared_chunk_manager.update_chunk_size(shared_bandwidth_monitor, true, chunk_size);
                        
                        progress_delta += chunk_size;
                        
                        last_acknowledged_offset = chunk_offset + chunk_size;
                        in_flight_bytes.fetch_sub(chunk_size);
                        
                        it = in_flight_chunks.erase(it);
                    }
                    
                    ack_cv.notify_all();
                    // --- ack_mutex released ---
                    
                    if (progress_delta > 0 && progress_delta_callback) {
                        progress_delta_callback(progress_delta);
                    }
                }
            } catch (const std::exception& e) {
                ack_thread_error = e.what();
                ack_thread_failed = true;
                ack_cv.notify_all();
            } catch (...) {
                ack_thread_error = "Unknown error in ACK listener thread";
                ack_thread_failed = true;
                ack_cv.notify_all();
            }
        });

        // Background disk read-ahead thread
        reader_thread = std::thread([&]() {
            try {
                file::FileStream file_stream;
                file::FileAccessPattern access_pattern =
                    config_.internal.cache_hints && (start_offset == 0 && end_offset == total_size)
                        ? file::FileAccessPattern::Sequential
                        : (config_.internal.cache_hints ? file::FileAccessPattern::Random : file::FileAccessPattern::Normal);
                if (!file_stream.open_read(file_path, access_pattern)) {
                    throw FileException("Failed to open source file for reading: " + file_path);
                }
                
                uint64_t current_read_offset = start_offset;
                while (current_read_offset < end_offset && !cancel_requested_ && !ack_thread_failed.load()) {
                    while (is_file_paused(file_path) && !is_file_skipped(file_path) && !cancel_requested_ && !ack_thread_failed.load()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    if (is_file_skipped(file_path)) {
                        throw FileSkippedException("File skip requested by client: " + file_path);
                    }
                    size_t chunk_size = shared_chunk_manager.get_optimal_chunk_size(shared_bandwidth_monitor);
                    // Cap chunk size so it always fits inside the flow-control window;
                    // without this cap the window condition can never become true → deadlock.
                    chunk_size = (std::min)(chunk_size, max_chunk_for_window);
                    chunk_size = static_cast<size_t>((std::min)(static_cast<uint64_t>(chunk_size), end_offset - current_read_offset));
                    
                    auto buffer = buffer_pool_->acquire();
                    buffer->resize(chunk_size);
                    
                    size_t bytes_read = file_stream.read(current_read_offset, buffer->data(), chunk_size);
                    if (bytes_read == 0) {
                        buffer_pool_->release(std::move(buffer));
                        throw FileException("Unexpected end of source file during read-ahead");
                    }
                    
                    if (bytes_read < chunk_size) {
                        buffer->resize(bytes_read);
                    }
                    
                    ReadAheadChunk chunk;
                    chunk.offset = current_read_offset;
                    chunk.data = std::move(buffer);
                    chunk.is_last = is_final_range && (current_read_offset + bytes_read >= end_offset);
                    
                    read_queue.push(std::move(chunk));
                    current_read_offset += bytes_read;
                }
                read_queue.set_finished();
            } catch (const FileSkippedException& e) {
                reader_error = e.what();
                is_skipped_error = true;
                reader_failed = true;
                read_queue.set_finished();
            } catch (const std::exception& e) {
                reader_error = e.what();
                reader_failed = true;
                read_queue.set_finished();
            } catch (...) {
                reader_error = "Unknown error in read-ahead thread";
                reader_failed = true;
                read_queue.set_finished();
            }
        });
    } catch (...) {
        cancel_requested_ = true;
        ack_cv.notify_all();
        read_queue.set_finished();
        if (ack_thread.joinable()) {
            ack_thread.join();
        }
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        throw;
    }

    uint64_t bytes_sent = start_offset;
    
    try {
        ReadAheadChunk current_chunk;
        while (read_queue.pop(current_chunk)) {
            if (cancel_requested_) {
                throw FileException("Transfer cancelled");
            }
            
            if (ack_thread_failed.load()) {
                throw FileException("ACK thread failed: " + ack_thread_error);
            }

            std::vector<ReadAheadChunk> batch;
            batch.push_back(std::move(current_chunk));
            uint64_t batch_uncompressed_bytes = batch.front().data->size();
            bool batch_has_last = batch.front().is_last;

            while (batch.size() < max_batch_chunks &&
                   batch_uncompressed_bytes < max_batch_bytes &&
                   !batch_has_last) {
                ReadAheadChunk next_chunk;
                if (!read_queue.try_pop(next_chunk)) {
                    break;
                }
                batch_uncompressed_bytes += next_chunk.data->size();
                batch_has_last = next_chunk.is_last;
                batch.push_back(std::move(next_chunk));
            }

            // Flow Control: block if in-flight bytes exceed window
            std::unique_lock<std::mutex> lock(ack_mutex);
            ack_cv.wait(lock, [&]() {
                return in_flight_bytes.load() + batch_uncompressed_bytes <= max_window_bytes.load() ||
                       cancel_requested_ || 
                       ack_thread_failed.load();
            });
            
            if (cancel_requested_) {
                throw FileException("Transfer cancelled");
            }
            if (ack_thread_failed.load()) {
                throw FileException("ACK thread failed: " + ack_thread_error);
            }

            protocol::FileData data_msg;
            uint64_t batch_last_end = bytes_sent;
            size_t throttled_bytes = 0;

            auto append_payload = [&](ReadAheadChunk& chunk, protocol::FileData::Chunk* out_chunk) {
                const size_t original_size = chunk.data->size();
                std::vector<uint8_t> payload;
                bool compressed_payload = false;

                if (stream_hasher && original_size > 0) {
                    stream_hasher->update(chunk.data->data(), original_size);
                }

                if (compress) {
                    auto compressed_data = common::compress_buffer(chunk.data->data(), original_size);
                    if (compressed_data.size() < original_size &&
                        compressed_data.size() <= negotiated_max_chunk_size_) {
                        payload = std::move(compressed_data);
                        compressed_payload = true;
                    }
                }

                if (payload.empty() && original_size > 0) {
                    payload.resize(original_size);
                    fast_mem::fast_memcpy(payload.data(), chunk.data->data(), original_size);
                }

                if (out_chunk) {
                    out_chunk->offset = chunk.offset;
                    out_chunk->uncompressed_size = original_size;
                    out_chunk->data = std::move(payload);
                    out_chunk->compressed = compressed_payload;
                    out_chunk->is_last_chunk = chunk.is_last;
                } else {
                    data_msg.offset = chunk.offset;
                    data_msg.uncompressed_size = original_size;
                    data_msg.data = std::move(payload);
                    data_msg.compressed = compressed_payload;
                    data_msg.is_last_chunk = chunk.is_last;
                }

                in_flight_chunks[chunk.offset] = original_size;
                in_flight_bytes.fetch_add(original_size);
                batch_last_end = chunk.offset + original_size;
                throttled_bytes += original_size;
                buffer_pool_->release(std::move(chunk.data));
            };

            if (batch.size() == 1) {
                append_payload(batch.front(), nullptr);
            } else {
                data_msg.chunks.reserve(batch.size());
                for (auto& chunk : batch) {
                    protocol::FileData::Chunk out_chunk;
                    append_payload(chunk, &out_chunk);
                    data_msg.chunks.push_back(std::move(out_chunk));
                }
            }
            
            lock.unlock(); // Release lock before sending message
            
            send_message(data_msg);
            bytes_sent = batch_last_end;
            
            if (bandwidth_limiter_) {
                bandwidth_limiter_->throttle(throttled_bytes);
            }
        }
        
        // Wait for all in-flight packets to be acknowledged
        std::unique_lock<std::mutex> lock(ack_mutex);
        ack_cv.wait(lock, [&]() {
            return in_flight_bytes.load() == 0 || ack_thread_failed.load() || cancel_requested_.load();
        });

        if (cancel_requested_) {
            throw FileException("Transfer cancelled");
        }
        
        if (ack_thread_failed.load()) {
            throw FileException("ACK thread failed during final flush: " + ack_thread_error);
        }
        
    } catch (...) {
        // Signal all worker threads to stop before joining.
        // Order matters: set cancel_requested_ FIRST so the reader thread's inner
        // pause/skip spin-loop (which checks cancel_requested_) exits immediately,
        // then wake the ack thread via the cv before calling set_finished() which
        // unblocks any push() call stuck waiting for queue space.
        cancel_requested_ = true;
        if (async_socket_) {
            async_socket_->disconnect();
        }
        ack_cv.notify_all();         // Wake the ack thread's wait
        read_queue.set_finished();   // Unblock any reader thread stuck in push()
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        if (ack_thread.joinable()) {
            ack_thread.join();
        }
        throw;
    }
    
    // Join threads successfully
    if (reader_thread.joinable()) {
        reader_thread.join();
    }
    if (ack_thread.joinable()) {
        ack_thread.join();
    }
    
    if (reader_failed.load()) {
        if (is_skipped_error.load()) {
            throw FileSkippedException(reader_error);
        }
        throw FileException("Reader thread failed: " + reader_error);
    }
    if (ack_thread_failed.load()) {
        throw FileException("ACK thread failed: " + ack_thread_error);
    }
}

uint32_t Client::choose_parallel_stream_count(uint64_t transfer_size) const {
    if (transfer_size < 64ull * 1024ull * 1024ull) {
        return 1;
    }

    uint32_t streams = negotiated_parallel_streams_ == 0 ? 1 : negotiated_parallel_streams_;
    if (transfer_size < 1024ull * 1024ull * 1024ull) {
        streams = (std::min)(streams, 2u);
    }
    return (std::max)(1u, streams);
}

void Client::create_empty_directory(const std::string& remote_path) {
    protocol::FileRequest request;
    request.source_path = ".netcopy_empty_dir";
    request.destination_path = file::FileManager::join_path(remote_path, ".netcopy_empty_dir");
    request.recursive = false;
    request.resume_offset = 0;
    request.auto_create_directories = config_.auto_create_directories && server_allows_auto_create_directories_;
    send_message(request);

    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::FileResponse*>(response_msg.get());
    if (!response || !response->success) {
        throw FileException(response ? response->error_message : "Invalid directory creation response");
    }

    protocol::FileData data_msg;
    data_msg.offset = 0;
    data_msg.uncompressed_size = 0;
    data_msg.is_last_chunk = true;
    data_msg.compressed = false;
    send_message(data_msg);

    auto ack_msg = receive_message();
    auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
    if (!ack || !ack->success) {
        throw FileException("Directory creation failed: " + (ack ? ack->error_message : "No acknowledgment"));
    }
}

void Client::set_error(const std::string& error) {
    last_error_ = error;
}

void Client::request_cancel() {
    cancel_requested_ = true;
    // Close the socket so any thread blocked in receive_message()/execute_io_sync()
    // gets an immediate network error instead of blocking forever.
    if (async_socket_) {
        async_socket_->disconnect();
    }

    std::vector<Client*> workers;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers = active_workers_;
    }

    for (auto* worker : workers) {
        if (worker && worker != this) {
            worker->request_cancel();
        }
    }
}

void Client::register_worker(Client* worker) {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    if (cancel_requested_) {
        worker->request_cancel();
    }
    active_workers_.push_back(worker);
}

void Client::unregister_worker(Client* worker) {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    auto it = std::find(active_workers_.begin(), active_workers_.end(), worker);
    if (it != active_workers_.end()) {
        active_workers_.erase(it);
    }
}

void Client::clear_error() {
    last_error_.clear();
}

uint32_t Client::get_next_sequence_number() {
    return sequence_number_++;
}

void Client::trigger_webhook(const std::string& action, const std::string& source, const std::string& destination, const std::string& status, uint64_t bytes, const std::string& error_msg, uint32_t files_transferred) {
    if (config_.webhook_url.empty()) {
        return;
    }
    std::string url = config_.webhook_url;
    std::string session_id = session_id_;
    
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
        try {
            common::send_http_post(url, payload);
        } catch (...) {
            // Ignore background webhook sending errors
        }
    }).detach();
}

void Client::download_file(const std::string& remote_path, const std::string& local_path, bool resume) {
    if (!async_socket_) {
        throw NetworkException("Socket is not connected");
    }

    uint64_t total_bytes = 0;
    uint64_t bytes_received = 0;

    try {
        protocol::DownloadRequest request;
        request.remote_path = remote_path;
        
        if (resume && std::filesystem::exists(std::filesystem::u8path(local_path))) {
            bytes_received = std::filesystem::file_size(std::filesystem::u8path(local_path));
        }
        request.resume_offset = bytes_received;

        send_message(request);

        auto response_msg = receive_message();
        auto response = dynamic_cast<protocol::DownloadResponse*>(response_msg.get());
        if (!response) {
            throw ProtocolException("Expected DownloadResponse");
        }
        session_id_ = response->session_id;
        if (parent_client_) {
            std::lock_guard<std::mutex> lock(parent_client_->control_mutex_);
            parent_client_->session_id_ = response->session_id;
        }

        if (!response->success) {
            throw FileException("Download failed: " + response->error_message);
        }

        if (response->is_directory) {
            file::FileManager::create_directories(local_path);
            trigger_webhook("download", remote_path, local_path, "success", 0);
            return;
        }

        if (response->is_symlink) {
            // Receive the empty FileData chunk to align the connection state
            auto msg = receive_message();
            auto file_data = dynamic_cast<protocol::FileData*>(msg.get());
            if (!file_data) {
                throw ProtocolException("Expected FileData");
            }
            
            // Delete existing file/symlink if it exists to avoid conflicts
            std::error_code ec;
            if (std::filesystem::exists(local_path, ec) || std::filesystem::is_symlink(local_path, ec)) {
                std::filesystem::remove(local_path, ec);
            }
            
            if (!file::FileManager::create_symlink(response->symlink_target, local_path)) {
                protocol::FileAck ack;
                ack.success = false;
                ack.bytes_received = 0;
                ack.error_message = "Failed to create symlink locally";
                send_message(ack);
                throw FileException("Failed to create symlink locally");
            }
            
            if (!file::FileManager::is_symlink(local_path)) {
                LOG_WARNING("Created placeholder file for symlink (privilege restrictions): " + local_path + " -> " + response->symlink_target);
            } else {
                LOG_DEBUG("Successfully created symlink: " + local_path + " -> " + response->symlink_target);
            }
            
            if (response->permissions != 0) {
                file::FileManager::set_permissions(local_path, response->permissions);
            }
            
            // Send success ACK
            protocol::FileAck ack;
            ack.success = true;
            ack.bytes_received = 0;
            send_message(ack);
            trigger_webhook("download", remote_path, local_path, "success", 0);
            return;
        }

        if (is_file_skipped(remote_path)) {
            throw FileSkippedException("File skip requested by client");
        }

        // Open local file
        file::FileStream fs;
        file::FileAccessPattern write_pattern = config_.internal.cache_hints
            ? file::FileAccessPattern::Sequential
            : file::FileAccessPattern::Normal;
        if (!fs.open_write(local_path, !resume, true, write_pattern)) {
            throw FileException("Failed to open local file for writing: " + local_path);
        }

        total_bytes = response->file_size;
        if (config_.internal.preallocate_files && !resume && total_bytes > 0) {
            std::string prealloc_error;
            if (!file::FileManager::preallocate_file(local_path, total_bytes, true, false, &prealloc_error)) {
                LOG_WARNING("Download preallocation skipped: " + prealloc_error);
            }
        }

        crypto::Sha3Hasher download_hasher;
        bool streaming_hash_valid = config_.internal.streaming_verification && !resume;

        while (bytes_received < total_bytes) {
            while (is_file_paused(remote_path) && !is_file_skipped(remote_path) && !cancel_requested_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (is_file_skipped(remote_path)) {
                disconnect();
                throw FileSkippedException("File skip requested by client");
            }

            auto msg = receive_message();
            auto file_data = dynamic_cast<protocol::FileData*>(msg.get());
            if (!file_data) {
                throw ProtocolException("Expected FileData");
            }

            struct TempChunk {
                uint64_t offset;
                uint64_t uncompressed_size;
                const std::vector<uint8_t>& data;
                bool is_last_chunk;
                bool compressed;
            };

            std::vector<TempChunk> chunks_to_process;
            if (!file_data->chunks.empty()) {
                for (const auto& chunk : file_data->chunks) {
                    chunks_to_process.push_back({chunk.offset, chunk.uncompressed_size, chunk.data, chunk.is_last_chunk, chunk.compressed});
                }
            } else {
                chunks_to_process.push_back({file_data->offset, file_data->uncompressed_size, file_data->data, file_data->is_last_chunk, file_data->compressed});
            }

            bool saw_last_chunk = false;
            try {
                for (const auto& chunk : chunks_to_process) {
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

                    if (streaming_hash_valid && payload_size > 0) {
                        if (chunk.offset == bytes_received) {
                            download_hasher.update(payload_ptr, payload_size);
                        } else {
                            streaming_hash_valid = false;
                        }
                    }

                    fs.write(chunk.offset, payload_ptr, payload_size);
                    uint64_t end_offset = chunk.offset + payload_size;
                    if (end_offset > bytes_received) {
                        bytes_received = end_offset;
                    }
                    if (chunk.is_last_chunk) {
                        saw_last_chunk = true;
                    }
                }
            } catch (const std::exception& e) {
                protocol::FileAck ack;
                ack.success = false;
                ack.bytes_received = bytes_received;
                ack.error_message = std::string("Disk write failed: ") + e.what();
                send_message(ack);
                throw FileException("Failed to write data to local file");
            }

            // Send success ACK
            protocol::FileAck ack;
            ack.success = true;
            ack.bytes_received = bytes_received;
            send_message(ack);

            if (progress_callback_) {
                progress_callback_(bytes_received, total_bytes, remote_path);
            }

            if (saw_last_chunk) {
                break;
            }
        }
        fs.close();
        if (response->permissions != 0) {
            file::FileManager::set_permissions(local_path, response->permissions);
        }
        
        // E2E Integrity check for download
        if (total_bytes > 0) {
            LOG_INFO("Performing E2E integrity check for downloaded file: " + local_path);
            auto local_hash = streaming_hash_valid
                ? download_hasher.finalize()
                : file::FileManager::compute_file_hash(local_path);
            
            protocol::FileVerifyRequest verify_req;
            verify_req.file_path = remote_path;
            verify_req.expected_hash = local_hash;
            
            send_message(verify_req);
            
            auto verify_resp_msg = receive_message();
            auto verify_resp = dynamic_cast<protocol::FileVerifyResponse*>(verify_resp_msg.get());
            if (!verify_resp) {
                throw ProtocolException("Expected FileVerifyResponse");
            }
            
            if (!verify_resp->success) {
                throw FileException("Download integrity verification failed for " + local_path + ": " + verify_resp->error_message);
            }
            
            LOG_INFO("Download E2E Integrity verification succeeded for: " + local_path);
        }
        trigger_webhook("download", remote_path, local_path, "success", total_bytes);
    } catch (const std::exception& e) {
        trigger_webhook("download", remote_path, local_path, "failed", bytes_received, e.what());
        throw;
    }
}

std::vector<protocol::RemoteFileInfo> Client::list_remote_directory(const std::string& remote_path, bool recursive) {
    if (!async_socket_) {
        throw NetworkException("Socket is not connected");
    }

    protocol::ListRequest request;
    request.remote_path = remote_path;
    request.recursive = recursive;
    send_message(request);

    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::ListResponse*>(response_msg.get());
    if (!response) {
        throw ProtocolException("Expected ListResponse");
    }

    if (!response->success) {
        throw FileException("Listing failed: " + response->error_message);
    }

    return response->entries;
}

void Client::download_directory(const std::string& remote_path, const std::string& local_path, bool recursive, bool resume) {
    uint64_t total_bytes = 0;
    uint32_t files_transferred = 0;
    try {
        auto entries = list_remote_directory(remote_path, recursive);
        
        std::vector<std::pair<std::string, uint64_t>> files_to_report;
        for (const auto& entry : entries) {
            if (!entry.is_directory) {
                files_to_report.push_back({entry.path, entry.size});
                total_bytes += entry.size;
                files_transferred++;
            }
        }
        
        if (file_list_callback_) {
            file_list_callback_(files_to_report);
        }
        
        // Normalize remote_path using filesystem path
        std::filesystem::path p_remote = std::filesystem::u8path(remote_path).lexically_normal();
        std::string norm_remote = p_remote.u8string();
        while (!norm_remote.empty() && (norm_remote.back() == '/' || norm_remote.back() == '\\')) {
            norm_remote.pop_back();
        }
        std::string norm_remote_cmp = norm_remote;
#ifdef _WIN32
        // Windows is case-insensitive
        std::transform(norm_remote_cmp.begin(), norm_remote_cmp.end(), norm_remote_cmp.begin(), ::tolower);
#endif

        for (const auto& entry : entries) {
            std::filesystem::path p_entry = std::filesystem::u8path(entry.path).lexically_normal();
            std::string norm_entry = p_entry.u8string();
            std::string norm_entry_cmp = norm_entry;
#ifdef _WIN32
            std::transform(norm_entry_cmp.begin(), norm_entry_cmp.end(), norm_entry_cmp.begin(), ::tolower);
#endif

            std::string rel_path = entry.path;
            if (norm_entry_cmp.rfind(norm_remote_cmp, 0) == 0) {
                rel_path = norm_entry.substr(norm_remote.length());
            }
            while (!rel_path.empty() && (rel_path[0] == '/' || rel_path[0] == '\\')) {
                rel_path = rel_path.substr(1);
            }
            
            std::string full_local = file::FileManager::join_path(local_path, rel_path);
            
            if (entry.is_directory) {
                file::FileManager::create_directories(full_local);
            } else {
                std::string parent_dir = file::FileManager::get_directory(full_local);
                if (!parent_dir.empty()) {
                    file::FileManager::create_directories(parent_dir);
                }
                try {
                    download_file(entry.path, full_local, resume);
                } catch (const FileSkippedException& e) {
                    LOG_INFO("File skipped: " + entry.path);
                    std::error_code ec;
                    std::filesystem::remove(full_local, ec);
                    disconnect();
                    connect(server_address_, server_port_);
                }
            }
        }
        trigger_webhook("download", remote_path, local_path, "success", total_bytes, "", files_transferred);
    } catch (const std::exception& e) {
        trigger_webhook("download", remote_path, local_path, "failed", total_bytes, e.what(), files_transferred);
        throw;
    }
}

} // namespace client
} // namespace netcopy
