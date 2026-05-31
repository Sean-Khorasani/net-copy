#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <functional>

namespace netcopy {
namespace file {

enum class FileAccessPattern {
    Normal,
    Sequential,
    Random
};

class FileManager {
public:
    struct FileInfo {
        std::string path;
        uint64_t size;
        bool is_directory;
        uint64_t last_modified;
        
        // Metadata fields
        uint32_t permissions = 0;
        bool is_symlink = false;
        std::string symlink_target;
    };

    struct BlockHash {
        uint64_t offset;
        std::vector<uint8_t> hash;
    };

    // File operations
    static bool exists(const std::string& path);
    static bool is_directory(const std::string& path);
    static bool is_regular_file(const std::string& path);
    static uint64_t file_size(const std::string& path);
    static uint64_t last_write_time(const std::string& path);
    static void set_last_write_time(const std::string& path, uint64_t last_modified);
    static uint32_t get_permissions(const std::string& path);
    static void set_permissions(const std::string& path, uint32_t permissions);
    static bool is_symlink(const std::string& path);
    static std::string read_symlink(const std::string& path);
    static bool create_symlink(const std::string& target, const std::string& symlink_path);
    
    // Directory operations
    static bool create_directories(const std::string& path);
    static std::vector<FileInfo> list_directory(const std::string& path, bool recursive = false);
    
    // File I/O
    static std::vector<uint8_t> read_file_chunk(const std::string& path, uint64_t offset, size_t chunk_size = 0);
    static void write_file_chunk(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data, bool auto_create = true, bool truncate_on_zero = true);
    static void create_file(const std::string& path, uint64_t size = 0, bool auto_create = true);
    static bool preallocate_file(const std::string& path, uint64_t size, bool auto_create = true, bool allow_set_valid_data = false, std::string* error_message = nullptr);
    static std::vector<uint8_t> compute_file_hash(const std::string& path, const std::function<bool()>& should_cancel = {});
    static std::vector<BlockHash> compute_block_hashes(const std::string& path, uint64_t block_size, const std::function<bool()>& should_cancel = {}, std::vector<uint8_t>* file_hash = nullptr);

    // Adaptive block size for delta-sync: balances granularity with hash overhead.
    // Returns a block size between 64 KB and 8 MB scaled by total file size.
    static uint64_t compute_optimal_block_size(uint64_t file_size);
    
    // Resume support
    static uint64_t get_partial_file_size(const std::string& path);
    static bool is_transfer_complete(const std::string& path, uint64_t expected_size);
    
    // Synchronization support
    enum class ConflictResolution {
        OVERWRITE,
        KEEP_BOTH,
        SKIP
    };
    
    struct SyncState {
        std::string local_path;
        std::string remote_path;
        uint64_t last_sync_time;
        bool is_up_to_date;
        std::vector<FileInfo> files_to_transfer;
        ConflictResolution conflict_policy;
    };
    
    // Directory comparison methods for synchronization
    static std::vector<FileInfo> compare_directories(const std::string& local_dir, const std::string& remote_dir);
    static std::vector<FileInfo> find_differences(const std::string& local_dir, const std::string& remote_dir);
    static void resolve_conflict(const FileInfo& file_info, ConflictResolution resolution);
    
    // Path utilities
    static std::string normalize_path(const std::string& path);
    static std::string get_filename(const std::string& path);
    static std::string get_directory(const std::string& path);
    static std::string join_path(const std::string& base, const std::string& relative);
    
    // Security
    static bool is_path_safe(const std::string& path, const std::string& base_directory);
    static std::string sanitize_filename(const std::string& filename);

private:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 262144; // 256KB
};

class FileStream {
public:
    FileStream();
    ~FileStream();
    
    // Non-copyable
    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;
    
    // Movable
    FileStream(FileStream&& other) noexcept;
    FileStream& operator=(FileStream&& other) noexcept;
    
    bool open_read(const std::string& path, FileAccessPattern access_pattern = FileAccessPattern::Normal);
    bool open_write(const std::string& path, bool truncate_on_zero = true, bool auto_create = true, FileAccessPattern access_pattern = FileAccessPattern::Normal);
    
    size_t read(uint64_t offset, uint8_t* buffer, size_t size);
    void write(uint64_t offset, const uint8_t* data, size_t size);
    
    void close();
    bool is_open() const;
    std::string get_path() const { return path_; }

private:
#ifdef _WIN32
    void* file_handle_;
#else
    int fd_;
#endif
    std::string path_;
};

} // namespace file
} // namespace netcopy
