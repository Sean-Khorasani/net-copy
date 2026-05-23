#pragma once

#include "network/socket.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/crypto_engine.h"
#include "config/config_parser.h"
#include "protocol/message.h"
#include "common/chunk_size_manager.h"
#include "common/bandwidth_limiter.h"
#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <map>

namespace netcopy {
namespace client {

class BufferPool {
public:
    BufferPool(size_t buffer_size, size_t max_buffers = 16)
        : buffer_size_(buffer_size) {
        for (size_t i = 0; i < max_buffers; ++i) {
            pool_.push_back(std::make_unique<std::vector<uint8_t>>(buffer_size_));
        }
    }
    
    ~BufferPool() = default;

    std::unique_ptr<std::vector<uint8_t>> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.empty()) {
            return std::make_unique<std::vector<uint8_t>>(buffer_size_);
        }
        auto buf = std::move(pool_.back());
        pool_.pop_back();
        return buf;
    }

    void release(std::unique_ptr<std::vector<uint8_t>> buf) {
        if (!buf) return;
        std::lock_guard<std::mutex> lock(mutex_);
        buf->resize(buffer_size_);
        pool_.push_back(std::move(buf));
    }

private:
    size_t buffer_size_;
    std::vector<std::unique_ptr<std::vector<uint8_t>>> pool_;
    std::mutex mutex_;
};

struct ReadAheadChunk {
    uint64_t offset;
    std::unique_ptr<std::vector<uint8_t>> data;
    bool is_last;
};

class ReadAheadQueue {
public:
    ReadAheadQueue(size_t max_size) : max_size_(max_size), finished_(false) {}
    ~ReadAheadQueue() = default;
    
    void push(ReadAheadChunk chunk) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_producer_.wait(lock, [this]() { return queue_.size() < max_size_ || finished_; });
        if (finished_) return;
        queue_.push(std::move(chunk));
        cv_consumer_.notify_one();
    }
    
    bool pop(ReadAheadChunk& chunk) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_consumer_.wait(lock, [this]() { return !queue_.empty() || finished_; });
        if (queue_.empty() && finished_) return false;
        chunk = std::move(queue_.front());
        queue_.pop();
        cv_producer_.notify_one();
        return true;
    }
    
    void set_finished() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        cv_producer_.notify_all();
        cv_consumer_.notify_all();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) queue_.pop();
        finished_ = false;
    }

private:
    std::queue<ReadAheadChunk> queue_;
    size_t max_size_;
    bool finished_;
    std::mutex mutex_;
    std::condition_variable cv_producer_;
    std::condition_variable cv_consumer_;
};

class Client {
public:
    Client();
    ~Client();
    
    // Configuration
    void load_config(const std::string& config_file);
    void set_config(const config::ClientConfig& config);
    const config::ClientConfig& get_config() const;
    
    // Connection management
    void connect(const std::string& server_address, uint16_t port);
    void disconnect();
    bool is_connected() const;
    
    // Security settings
    void set_security_level(crypto::SecurityLevel level);
    
    // File transfer
    void transfer_file(const std::string& local_path, const std::string& remote_path, bool resume = false);
    void transfer_directory(const std::string& local_path, const std::string& remote_path, bool recursive = true, bool resume = false);
    void download_file(const std::string& remote_path, const std::string& local_path);
    void download_directory(const std::string& remote_path, const std::string& local_path, bool recursive = true);
    std::vector<protocol::RemoteFileInfo> list_remote_directory(const std::string& remote_path, bool recursive = false);
    
    // Progress callback
    using ProgressCallback = std::function<void(uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file)>;
    void set_progress_callback(ProgressCallback callback);
    
    // Error handling
    std::string get_last_error() const;
    void request_cancel();

    // Buffer pool support for parallel streams
    void set_buffer_pool(std::shared_ptr<BufferPool> pool) { buffer_pool_ = pool; }

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
    size_t negotiated_max_chunk_size_;
    uint32_t requested_parallel_streams_;
    uint32_t negotiated_parallel_streams_;
    bool server_allows_auto_create_directories_;
    std::atomic<bool> cancel_requested_;
    std::string server_address_;
    uint16_t server_port_;
    
    // Buffer Pool sharing
    std::shared_ptr<BufferPool> buffer_pool_;
    
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
    void send_file_range(const std::string& file_path,
                         uint64_t start_offset,
                         uint64_t end_offset,
                         uint64_t total_size,
                         common::ChunkSizeManager& shared_chunk_manager,
                         common::BandwidthMonitor& shared_bandwidth_monitor,
                         const std::function<void(uint64_t)>& progress_delta_callback);
    uint32_t choose_parallel_stream_count(uint64_t transfer_size) const;
    void send_file_request(const std::string& local_path,
                           const std::string& remote_path,
                           bool resume,
                           bool truncate_destination,
                           uint64_t& resume_offset);
    
    // Directory management
    void create_empty_directory(const std::string& remote_path);
    
    // Utility functions
    void set_error(const std::string& error);
    void clear_error();
    uint32_t get_next_sequence_number();

    // Adaptive chunk size management
    common::ChunkSizeManager chunk_size_manager_;
    common::BandwidthMonitor bandwidth_monitor_;
    std::shared_ptr<common::BandwidthLimiter> bandwidth_limiter_;
};

} // namespace client
} // namespace netcopy
