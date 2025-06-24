#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdint>

namespace netcopy {
namespace file {

class FileManager {
public:
    struct FileInfo {
        std::string path;
        uint64_t size;
        bool is_directory;
        uint64_t last_modified;
    };

    // File operations
    static bool exists(const std::string& path);
    static bool is_directory(const std::string& path);
    static bool is_regular_file(const std::string& path);
    static uint64_t file_size(const std::string& path);
    static uint64_t last_write_time(const std::string& path);
    
    // Directory operations
    static bool create_directories(const std::string& path);
    static std::vector<FileInfo> list_directory(const std::string& path, bool recursive = false);
    
    // File I/O
    static std::vector<uint8_t> read_file_chunk(const std::string& path, uint64_t offset, size_t chunk_size);
    static void write_file_chunk(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data);
    static void create_file(const std::string& path, uint64_t size = 0);
    
    // Resume support
    static uint64_t get_partial_file_size(const std::string& path);
    static bool is_transfer_complete(const std::string& path, uint64_t expected_size);
    
    // Path utilities
    static std::string normalize_path(const std::string& path);
    static std::string get_filename(const std::string& path);
    static std::string get_directory(const std::string& path);
    static std::string join_path(const std::string& base, const std::string& relative);
    
    // Security
    static bool is_path_safe(const std::string& path, const std::string& base_directory);
    static std::string sanitize_filename(const std::string& filename);

private:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 65536; // 64KB
};

} // namespace file
} // namespace netcopy

