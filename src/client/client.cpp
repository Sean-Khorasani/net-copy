#include "client/client.h"
#include "common/compression.h"
#include "common/utils.h"
#include "exceptions.h"
#include "file/file_manager.h"
#include "logging/logger.h"
#include "crypto/sha3.h"
#include "crypto/mlkem.h"
#include "crypto/key_manager.h"
#include "protocol/message.h"
#include <algorithm>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace netcopy {
namespace client {

Client::Client()
    : config_(config::ClientConfig::get_default()),
      connected_(false),
      sequence_number_(1),
      security_level_(crypto::SecurityLevel::HIGH),
      negotiated_security_level_(crypto::SecurityLevel::HIGH),
      negotiated_max_chunk_size_(config_.max_chunk_size),
      requested_parallel_streams_((std::max)(1u, (std::min)(8u, std::thread::hardware_concurrency() == 0 ? 1u : std::thread::hardware_concurrency()))),
      negotiated_parallel_streams_(1),
      server_allows_auto_create_directories_(false),
      cancel_requested_(false),
      server_port_(0),
      bandwidth_limiter_(std::make_shared<common::BandwidthLimiter>()) {
    chunk_size_manager_.set_limits(config_.initial_chunk_size, config_.min_chunk_size, config_.max_chunk_size);
    chunk_size_manager_.set_adaptation_parameters(
        config_.chunk_size_increase_factor,
        config_.chunk_size_decrease_factor,
        0.3);
    bandwidth_limiter_->set_limit_percent(config_.max_bandwidth_percent);
}

Client::~Client() {
    disconnect();
}

void Client::load_config(const std::string& config_file) {
    config_ = config::ClientConfig::load_from_file(config_file);

    auto& logger = logging::Logger::instance();
    logger.set_level(logging::Logger::string_to_level(config_.log_level));
    logger.set_console_output(config_.console_output);
    if (!config_.log_file.empty()) {
        logger.set_file_output(config_.log_file);
    }

    chunk_size_manager_.set_limits(config_.initial_chunk_size, config_.min_chunk_size, config_.max_chunk_size);
    chunk_size_manager_.set_adaptation_parameters(
        config_.chunk_size_increase_factor,
        config_.chunk_size_decrease_factor,
        0.3);
    negotiated_max_chunk_size_ = config_.max_chunk_size;
    server_allows_auto_create_directories_ = false;
    cancel_requested_ = false;
    bandwidth_limiter_->set_limit_percent(config_.max_bandwidth_percent);
}

void Client::set_config(const config::ClientConfig& config) {
    config_ = config;
    chunk_size_manager_.set_limits(config_.initial_chunk_size, config_.min_chunk_size, config_.max_chunk_size);
    chunk_size_manager_.set_adaptation_parameters(
        config_.chunk_size_increase_factor,
        config_.chunk_size_decrease_factor,
        0.3);
    negotiated_max_chunk_size_ = config_.max_chunk_size;
    server_allows_auto_create_directories_ = false;
    cancel_requested_ = false;
    bandwidth_limiter_->set_limit_percent(config_.max_bandwidth_percent);
}

const config::ClientConfig& Client::get_config() const {
    return config_;
}

void Client::connect(const std::string& server_address, uint16_t port) {
    try {
        socket_ = std::make_unique<network::Socket>();
        // Apply timeout only for the connect/handshake phase.
        socket_->set_timeout(config_.timeout);
        socket_->connect(server_address, port);
        
        // Disable Nagle's algorithm and tune buffer sizes to 4MB
        socket_->set_tcp_nodelay(true);
        socket_->set_buffer_sizes(4 * 1024 * 1024, 4 * 1024 * 1024);
        
        server_address_ = server_address;
        server_port_ = port;
        perform_handshake();

        // Remove the timeout after handshake so data-transfer I/O (ACK
        // listener etc.) is never cut short by SO_RCVTIMEO/SO_SNDTIMEO.
        // The OS TCP keep-alive and explicit flow-control handle stalls.
        socket_->set_timeout(0);

        connected_ = true;
        clear_error();
    } catch (const std::exception& e) {
        connected_ = false;
        set_error(e.what());
        socket_.reset();
        throw;
    }
}

void Client::disconnect() {
    if (socket_) {
        socket_->close();
    }
    socket_.reset();
    connected_ = false;
}

bool Client::is_connected() const {
    return connected_;
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

    transfer_single_file(local_path, remote_path, resume);
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

    auto files = file::FileManager::list_directory(local_path, recursive);
    for (const auto& entry : files) {
        std::string relative = entry.path;
        if (relative.find(local_path) == 0) {
            relative = relative.substr(local_path.length());
            while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
                relative.erase(relative.begin());
            }
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
            transfer_single_file(entry.path, destination, resume);
        }
    }
}

void Client::set_progress_callback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
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
    request.username = config_.username;
    if (config_.auth_method == "password")      request.auth_method_id = 1;
    else if (config_.auth_method == "mlkem")    request.auth_method_id = 2;
    else                                         request.auth_method_id = 0;

    send_message(request);

    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::HandshakeResponse*>(response_msg.get());
    if (!response || !response->authentication_required && response->server_version.empty()) {
        throw ProtocolException("Invalid handshake response");
    }

    negotiated_security_level_ = response->accepted_security_level;
    negotiated_max_chunk_size_ = response->max_chunk_size == 0
        ? config_.max_chunk_size
        : (std::min)(config_.max_chunk_size, static_cast<size_t>(response->max_chunk_size));
    negotiated_parallel_streams_ = response->accepted_parallel_streams == 0 ? 1 : response->accepted_parallel_streams;
    server_allows_auto_create_directories_ = response->auto_create_directories_allowed;
    chunk_size_manager_.set_max_chunk_size(negotiated_max_chunk_size_);
    if (response->authentication_required) {
        if (config_.secret_key.empty()) {
            throw CryptoException("Server requires authentication but no secret key is configured");
        }
        crypto_engine_ = crypto::create_crypto_engine(negotiated_security_level_, config_.secret_key);
    }

    // User authentication phase
    if (!config_.username.empty() && config_.auth_method != "none" && request.auth_method_id != 0) {
        auto challenge_msg = receive_message();
        auto* auth_challenge = dynamic_cast<protocol::AuthChallenge*>(challenge_msg.get());
        if (!auth_challenge) {
            throw ProtocolException("Expected AuthChallenge from server");
        }

        std::vector<uint8_t> proof;

        if (auth_challenge->method == 1) { // PASSWORD
            // Derive key from password
            auto salt = crypto::hex_to_bytes(auth_challenge->salt_hex);
            auto dk = crypto::pbkdf2_sha3_256(config_.password, salt,
                                               auth_challenge->pbkdf2_iterations, 32);
            // proof = SHA3-256(dk || challenge_nonce)
            std::vector<uint8_t> preimage = dk;
            preimage.insert(preimage.end(),
                            auth_challenge->challenge_nonce.begin(),
                            auth_challenge->challenge_nonce.end());
            proof = crypto::sha3_256(preimage);

        } else if (auth_challenge->method == 2) { // ML-KEM
            if (config_.private_key_file.empty()) {
                throw CryptoException("ML-KEM private key file not configured");
            }
            crypto::MlKemLevel level;
            auto privkey = crypto::load_private_key(config_.private_key_file, level,
                                                     config_.private_key_passphrase);
            auto shared_secret = crypto::MlKem::decapsulate(privkey,
                                                             auth_challenge->kem_ciphertext,
                                                             level);
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
        LOG_INFO("Authenticated as '" + config_.username + "'");
    }
}

void Client::send_message(const protocol::Message& message) {
    if (!socket_) {
        throw NetworkException("Socket is not connected");
    }

    auto data = message.serialize();
    if (crypto_engine_) {
        data = encrypt_message(data);
    }

    uint32_t length = static_cast<uint32_t>(data.size());
    socket_->send(&length, sizeof(length));

    size_t total_sent = 0;
    while (total_sent < data.size()) {
        total_sent += socket_->send(data.data() + total_sent, data.size() - total_sent);
    }
}

std::unique_ptr<protocol::Message> Client::receive_message() {
    if (!socket_) {
        throw NetworkException("Socket is not connected");
    }

    uint32_t length = 0;
    socket_->receive(&length, sizeof(length));

    std::vector<uint8_t> data(length);
    size_t total_received = 0;
    while (total_received < data.size()) {
        total_received += socket_->receive(data.data() + total_received, data.size() - total_received);
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
    uint64_t total_size = file::FileManager::file_size(local_path);

    uint64_t resume_offset = 0;
    send_file_request(local_path, remote_path, resume, !resume, resume_offset);
    if (resume_offset > total_size) {
        throw FileException("Resume offset is larger than source file");
    }

    bandwidth_monitor_.reset();
    chunk_size_manager_.reset();

    uint32_t stream_count = resume ? 1 : choose_parallel_stream_count(total_size - resume_offset);
    
    // Initialize BufferPool for memory reuse across all streams
    buffer_pool_ = std::make_shared<BufferPool>(negotiated_max_chunk_size_, stream_count * 4);
    
    common::BandwidthMonitor transfer_monitor;
    auto make_chunk_manager = [&]() {
        return common::ChunkSizeManager(
            config_.initial_chunk_size,
            config_.min_chunk_size,
            negotiated_max_chunk_size_,
            config_.chunk_size_increase_factor,
            config_.chunk_size_decrease_factor,
            0.3);
    };
    if (stream_count <= 1 || total_size == resume_offset) {
        send_file_data(local_path, resume_offset, total_size);
        return;
    }

    std::atomic<uint64_t> next_offset(resume_offset);
    std::atomic<uint64_t> transferred(resume_offset);
    std::exception_ptr first_error;
    std::mutex error_mutex;
    std::mutex progress_mutex;
    const uint64_t range_granularity = (std::max<uint64_t>)(negotiated_max_chunk_size_, config_.initial_chunk_size);

    auto record_error = [&](std::exception_ptr error) {
        std::lock_guard<std::mutex> lock(error_mutex);
        if (!first_error) {
            first_error = error;
        }
    };

    auto make_progress_callback = [&]() {
        return [&](uint64_t delta) {
            uint64_t current = transferred.fetch_add(delta) + delta;
            if (progress_callback_) {
                std::lock_guard<std::mutex> lock(progress_mutex);
                progress_callback_(current, total_size, local_path);
            }
        };
    };

    auto worker_body = [&](uint32_t stream_index) {
        try {
            Client stream_client;
            stream_client.set_config(config_);
            stream_client.set_security_level(security_level_);
            stream_client.set_buffer_pool(buffer_pool_); // Propagate buffer pool
            stream_client.bandwidth_limiter_ = bandwidth_limiter_;
            stream_client.connect(server_address_, server_port_);
            common::ChunkSizeManager worker_chunk_manager = make_chunk_manager();

            uint64_t worker_resume_offset = 0;
            stream_client.send_file_request(local_path, remote_path, false, false, worker_resume_offset);

            while (true) {
                uint64_t start = next_offset.fetch_add(range_granularity);
                if (start >= total_size) {
                    break;
                }
                uint64_t end = (std::min)(start + range_granularity, total_size);
                stream_client.send_file_range(local_path,
                                               start,
                                               end,
                                               total_size,
                                               worker_chunk_manager,
                                               transfer_monitor,
                                               make_progress_callback());
            }
        } catch (...) {
            record_error(std::current_exception());
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(stream_count - 1);
    for (uint32_t i = 1; i < stream_count; ++i) {
        workers.emplace_back(worker_body, i);
    }

    try {
        common::ChunkSizeManager main_chunk_manager = make_chunk_manager();
        while (true) {
            uint64_t start = next_offset.fetch_add(range_granularity);
            if (start >= total_size) {
                break;
            }
            uint64_t end = (std::min)(start + range_granularity, total_size);
            send_file_range(local_path,
                            start,
                            end,
                            total_size,
                            main_chunk_manager,
                            transfer_monitor,
                            make_progress_callback());
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
}

void Client::send_file_request(const std::string& local_path,
                               const std::string& remote_path,
                               bool resume,
                               bool truncate_destination,
                               uint64_t& resume_offset) {
    protocol::FileRequest request;
    request.source_path = common::convert_to_unix_path(local_path);
    request.destination_path = common::convert_to_unix_path(remote_path);
    request.recursive = false;
    request.resume_offset = resume ? 1 : 0;
    request.auto_create_directories = config_.auto_create_directories;
    request.truncate_destination = truncate_destination;
    send_message(request);

    auto response_msg = receive_message();
    auto response = dynamic_cast<protocol::FileResponse*>(response_msg.get());
    if (!response) {
        throw ProtocolException("Invalid file response");
    }
    if (!response->success) {
        throw FileException(response->error_message);
    }

    resume_offset = resume ? response->resume_offset : 0;
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
    });
}

void Client::send_file_range(const std::string& file_path,
                             uint64_t start_offset,
                             uint64_t end_offset,
                             uint64_t total_size,
                             common::ChunkSizeManager& shared_chunk_manager,
                             common::BandwidthMonitor& shared_bandwidth_monitor,
                             const std::function<void(uint64_t)>& progress_delta_callback) {
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
    
    // Background ACK listener thread
    std::thread ack_thread([&]() {
        try {
            while (last_acknowledged_offset.load() < end_offset && !cancel_requested_ && !ack_thread_failed.load()) {
                auto ack_msg = receive_message();
                auto ack = dynamic_cast<protocol::FileAck*>(ack_msg.get());
                if (!ack || !ack->success) {
                    throw FileException("Transfer failed: " + (ack ? ack->error_message : "No acknowledgment"));
                }
                
                uint64_t bytes_received = ack->bytes_received;
                
                std::lock_guard<std::mutex> lock(ack_mutex);
                auto it = in_flight_chunks.begin();
                while (it != in_flight_chunks.end() && it->first + it->second <= bytes_received) {
                    uint64_t chunk_offset = it->first;
                    size_t chunk_size = it->second;
                    
                    shared_bandwidth_monitor.record_bytes(chunk_size);
                    shared_chunk_manager.update_chunk_size(shared_bandwidth_monitor, true, chunk_size);
                    
                    if (progress_delta_callback) {
                        progress_delta_callback(chunk_size);
                    }
                    
                    last_acknowledged_offset = chunk_offset + chunk_size;
                    in_flight_bytes.fetch_sub(chunk_size);
                    
                    it = in_flight_chunks.erase(it);
                }
                
                ack_cv.notify_all();
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

    // Flow-control window: keep up to 64 MB in flight to allow pipelining.
    // The chunk size is capped to at most half this value so the wait
    // condition (in_flight + chunk_size <= window) is always reachable.
    const uint64_t max_window_bytes = 64 * 1024 * 1024; // 64 MB in-flight window
    const size_t max_chunk_for_window = static_cast<size_t>(max_window_bytes / 2); // 32 MB per chunk max

    // Background disk read-ahead thread
    ReadAheadQueue read_queue(4); // Keep at most 4 chunks pre-read in memory
    std::atomic<bool> reader_failed(false);
    std::string reader_error;
    
    std::thread reader_thread([&]() {
        try {
            file::FileStream file_stream;
            if (!file_stream.open_read(file_path)) {
                throw FileException("Failed to open source file for reading: " + file_path);
            }
            
            uint64_t current_read_offset = start_offset;
            while (current_read_offset < end_offset && !cancel_requested_ && !ack_thread_failed.load()) {
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
                chunk.is_last = (current_read_offset + bytes_read >= total_size);
                
                read_queue.push(std::move(chunk));
                current_read_offset += bytes_read;
            }
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
            
            // Flow Control: block if in-flight bytes exceed window
            std::unique_lock<std::mutex> lock(ack_mutex);
            ack_cv.wait(lock, [&]() {
                return in_flight_bytes.load() + current_chunk.data->size() <= max_window_bytes || 
                       cancel_requested_ || 
                       ack_thread_failed.load();
            });
            
            if (cancel_requested_) {
                throw FileException("Transfer cancelled");
            }
            if (ack_thread_failed.load()) {
                throw FileException("ACK thread failed: " + ack_thread_error);
            }
            
            // Build FILE_DATA message
            protocol::FileData data_msg;
            data_msg.offset = current_chunk.offset;
            data_msg.uncompressed_size = current_chunk.data->size();
            
            std::vector<uint8_t> final_data;
            if (compress) {
                auto compressed_data = common::compress_buffer(*current_chunk.data);
                if (compressed_data.size() < current_chunk.data->size() &&
                    compressed_data.size() <= negotiated_max_chunk_size_) {
                    final_data = std::move(compressed_data);
                    data_msg.compressed = true;
                } else {
                    final_data = *current_chunk.data;
                    data_msg.compressed = false;
                }
            } else {
                final_data = *current_chunk.data;
                data_msg.compressed = false;
            }
            
            data_msg.data = std::move(final_data);
            data_msg.is_last_chunk = current_chunk.is_last;
            
            // Record in-flight details before writing to socket
            uint64_t chunk_offset = current_chunk.offset;
            size_t chunk_size = current_chunk.data->size();
            
            in_flight_chunks[chunk_offset] = chunk_size;
            in_flight_bytes.fetch_add(chunk_size);
            
            // Return buffer to pool
            buffer_pool_->release(std::move(current_chunk.data));
            
            lock.unlock(); // Release lock before sending message
            
            send_message(data_msg);
            bytes_sent = chunk_offset + chunk_size;
            
            if (bandwidth_limiter_) {
                bandwidth_limiter_->throttle(chunk_size);
            }
        }
        
        // Wait for all in-flight packets to be acknowledged
        std::unique_lock<std::mutex> lock(ack_mutex);
        ack_cv.wait(lock, [&]() {
            return in_flight_bytes.load() == 0 || ack_thread_failed.load();
        });
        
        if (ack_thread_failed.load()) {
            throw FileException("ACK thread failed during final flush: " + ack_thread_error);
        }
        
    } catch (...) {
        // Shut down worker threads
        cancel_requested_ = true;
        read_queue.set_finished();
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
}

void Client::clear_error() {
    last_error_.clear();
}

uint32_t Client::get_next_sequence_number() {
    return sequence_number_++;
}

} // namespace client
} // namespace netcopy
