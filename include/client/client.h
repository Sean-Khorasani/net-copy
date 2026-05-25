#pragma once

#include "network/socket.h"
#include "crypto/chacha20_poly1305.h"
#include "crypto/crypto_engine.h"
#include "config/config_parser.h"
#include "protocol/message.h"
#include "common/chunk_size_manager.h"
#include "common/bandwidth_limiter.h"
#include "common/fast_mem.h"
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
    struct AlignedBuffer {
        uint8_t* ptr;
        size_t size_;
        size_t capacity_;
        BufferPool* pool;

        AlignedBuffer(uint8_t* p, size_t cap, BufferPool* pl)
            : ptr(p), size_(cap), capacity_(cap), pool(pl) {}

        ~AlignedBuffer() {
            if (ptr && pool) {
                pool->deallocate(ptr);
            } else if (ptr) {
                #if defined(_MSC_VER) || defined(__MINGW32__)
                _aligned_free(ptr);
                #else
                free(ptr);
                #endif
            }
        }

        uint8_t* data() { return ptr; }
        const uint8_t* data() const { return ptr; }
        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }
        void resize(size_t new_size) {
            if (new_size > capacity_) throw std::bad_alloc();
            size_ = new_size;
        }
    };

    BufferPool(size_t buffer_size, size_t max_buffers = 16)
        : buffer_size_(buffer_size),
          pool_allocator_(buffer_size, max_buffers, 64) {
    }
    
    ~BufferPool() = default;

    std::unique_ptr<AlignedBuffer> acquire() {
        void* ptr = pool_allocator_.allocate();
        if (!ptr) {
            // Fallback: allocate 64-byte aligned memory on the fly
            #if defined(_MSC_VER) || defined(__MINGW32__)
            ptr = _aligned_malloc(buffer_size_, 64);
            #else
            if (posix_memalign(&ptr, 64, buffer_size_) != 0) ptr = nullptr;
            #endif
            if (!ptr) throw std::bad_alloc();
            return std::make_unique<AlignedBuffer>(static_cast<uint8_t*>(ptr), buffer_size_, nullptr);
        }
        return std::make_unique<AlignedBuffer>(static_cast<uint8_t*>(ptr), buffer_size_, this);
    }

    void release(std::unique_ptr<AlignedBuffer> buf) {
        // AlignedBuffer destructor handles recycling automatically.
        buf.reset();
    }

private:
    void deallocate(void* ptr) {
        pool_allocator_.deallocate(ptr);
    }

    size_t buffer_size_;
    fast_mem::PoolAllocator pool_allocator_;
};

struct ReadAheadChunk {
    uint64_t offset;
    std::unique_ptr<BufferPool::AlignedBuffer> data;
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
    void download_file(const std::string& remote_path, const std::string& local_path, bool resume = false);
    void download_directory(const std::string& remote_path, const std::string& local_path, bool recursive = true, bool resume = false);
    std::vector<protocol::RemoteFileInfo> list_remote_directory(const std::string& remote_path, bool recursive = false);
    
    // Progress callback
    using ProgressCallback = std::function<void(uint64_t bytes_transferred, uint64_t total_bytes, const std::string& current_file)>;
    void set_progress_callback(ProgressCallback callback);
    void set_parent_client(Client* parent) { parent_client_ = parent; }
    uint32_t get_negotiated_parallel_streams() const { return negotiated_parallel_streams_; }
    void set_requested_parallel_streams(uint32_t count) { requested_parallel_streams_ = count; }
    void register_worker(Client* worker);
    void unregister_worker(Client* worker);
    std::string get_session_id() const { return session_id_; }
    
    enum class OverwriteDecision {
        OVERWRITE,
        DELTA_SYNC,
        SKIP,
        CANCEL
    };
    using OverwriteCallback = std::function<OverwriteDecision(const std::string& remote_path, uint64_t remote_size)>;
    void set_overwrite_callback(OverwriteCallback callback);
    
    using FileListCallback = std::function<void(const std::vector<std::pair<std::string, uint64_t>>& files)>;
    void set_file_list_callback(FileListCallback callback);
    
    void pause_file(const std::string& path);
    void resume_file(const std::string& path);
    void skip_file(const std::string& path);
    bool is_file_paused(const std::string& path);
    bool is_file_skipped(const std::string& path);
    protocol::TransferStatusResponse query_transfer_status(const std::string& session_id);
    void create_empty_directory(const std::string& remote_path);
    
    // Error handling
    std::string get_last_error() const;
    void request_cancel();
    std::shared_ptr<common::BandwidthLimiter> bandwidth_limiter_;
    std::atomic<bool> cancel_requested_;

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
    OverwriteCallback overwrite_callback_;
    uint32_t sequence_number_;
    crypto::SecurityLevel security_level_;
    crypto::SecurityLevel negotiated_security_level_;
    size_t negotiated_max_chunk_size_;
    uint32_t requested_parallel_streams_;
    uint32_t negotiated_parallel_streams_;
    bool server_allows_auto_create_directories_;
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
                         const std::function<void(uint64_t)>& progress_delta_callback,
                         bool is_final_range = false);
    uint32_t choose_parallel_stream_count(uint64_t transfer_size) const;
    void send_file_request(const std::string& local_path,
                           const std::string& remote_path,
                           bool resume,
                           bool truncate_destination,
                           uint64_t& resume_offset,
                           uint64_t* remote_file_size = nullptr);
    
    // Utility functions
    void set_error(const std::string& error);
    void clear_error();
    uint32_t get_next_sequence_number();

    // Adaptive chunk size management
    common::ChunkSizeManager chunk_size_manager_;
    common::BandwidthMonitor bandwidth_monitor_;
    std::mutex workers_mutex_;
    std::vector<Client*> active_workers_;
    
    FileListCallback file_list_callback_;
    Client* parent_client_ = nullptr;
    std::mutex control_mutex_;
    std::map<std::string, bool> paused_files_;
    std::map<std::string, bool> skipped_files_;
    std::string session_id_;
};

} // namespace client
} // namespace netcopy
