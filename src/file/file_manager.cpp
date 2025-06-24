#include "file/file_manager.h"
#include "exceptions.h"
#include <algorithm>
#include <regex>
#include <vector> // Explicitly include vector here as well

namespace netcopy {
namespace file {

bool FileManager::exists(const std::string& path) {
    return std::filesystem::exists(path);
}

bool FileManager::is_directory(const std::string& path) {
    return std::filesystem::is_directory(path);
}

bool FileManager::is_regular_file(const std::string& path) {
    return std::filesystem::is_regular_file(path);
}

uint64_t FileManager::file_size(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw FileException("Failed to get file size for " + path + ": " + ec.message());
    }
    return size;
}

uint64_t FileManager::last_write_time(const std::string& path) {
    std::error_code ec;
    auto time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        throw FileException("Failed to get last write time for " + path + ": " + ec.message());
    }
    
    // Convert to Unix timestamp
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

bool FileManager::create_directories(const std::string& path) {
    std::error_code ec;
    return std::filesystem::create_directories(path, ec);
}

std::vector<FileManager::FileInfo> FileManager::list_directory(const std::string& path, bool recursive) {
    std::vector<FileInfo> files;
    
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
                FileInfo info;
                info.path = entry.path().string();
                info.is_directory = entry.is_directory();
                
                if (!info.is_directory) {
                    info.size = file_size(info.path);
                    info.last_modified = last_write_time(info.path);
                } else {
                    info.size = 0;
                    info.last_modified = 0;
                }
                
                files.push_back(info);
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                FileInfo info;
                info.path = entry.path().string();
                info.is_directory = entry.is_directory();
                
                if (!info.is_directory) {
                    info.size = file_size(info.path);
                    info.last_modified = last_write_time(info.path);
                } else {
                    info.size = 0;
                    info.last_modified = 0;
                }
                
                files.push_back(info);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        throw FileException("Failed to list directory " + path + ": " + e.what());
    }
    
    return files;
}

std::vector<uint8_t> FileManager::read_file_chunk(const std::string& path, uint64_t offset, size_t chunk_size) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FileException("Failed to open file for reading: " + path);
    }
    
    file.seekg(offset);
    if (!file) {
        throw FileException("Failed to seek to offset " + std::to_string(offset) + " in file: " + path);
    }
    
    std::vector<uint8_t> buffer(chunk_size);
    file.read(reinterpret_cast<char*>(buffer.data()), chunk_size);
    
    size_t bytes_read = file.gcount();
    buffer.resize(bytes_read);
    
    return buffer;
}

void FileManager::write_file_chunk(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data) {
    // Create directory if it doesn't exist
    auto dir = get_directory(path);
    if (!dir.empty() && !exists(dir)) {
        create_directories(dir);
    }
    
    std::ios::openmode mode = std::ios::binary | std::ios::in | std::ios::out;
    
    // If starting from offset 0, truncate the file (non-resume mode)
    if (offset == 0) {
        mode |= std::ios::trunc;
    }
    
    std::fstream file(path, mode);
    if (!file) {
        // File doesn't exist, create it
        file.open(path, std::ios::binary | std::ios::out);
        if (!file) {
            throw FileException("Failed to create file: " + path);
        }
        file.close();
        file.open(path, mode);
    }
    
    file.seekp(offset);
    if (!file) {
        throw FileException("Failed to seek to offset " + std::to_string(offset) + " in file: " + path);
    }
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!file) {
        throw FileException("Failed to write data to file: " + path);
    }
}

void FileManager::create_file(const std::string& path, uint64_t size) {
    auto dir = get_directory(path);
    if (!dir.empty() && !exists(dir)) {
        create_directories(dir);
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw FileException("Failed to create file: " + path);
    }
    
    if (size > 0) {
        file.seekp(size - 1);
        file.write("", 1);
    }
}

uint64_t FileManager::get_partial_file_size(const std::string& path) {
    if (!exists(path)) {
        return 0;
    }
    return file_size(path);
}

bool FileManager::is_transfer_complete(const std::string& path, uint64_t expected_size) {
    if (!exists(path)) {
        return false;
    }
    return file_size(path) == expected_size;
}

std::string FileManager::normalize_path(const std::string& path) {
    std::filesystem::path p(path);
    return p.lexically_normal().string();
}

std::string FileManager::get_filename(const std::string& path) {
    std::filesystem::path p(path);
    return p.filename().string();
}

std::string FileManager::get_directory(const std::string& path) {
    std::filesystem::path p(path);
    return p.parent_path().string();
}

std::string FileManager::join_path(const std::string& base, const std::string& relative) {
    std::filesystem::path p = std::filesystem::path(base) / relative;
    return p.string();
}

bool FileManager::is_path_safe(const std::string& path, const std::string& base_directory) {
    try {
        std::filesystem::path normalized_path = std::filesystem::path(path).lexically_normal();
        std::filesystem::path normalized_base = std::filesystem::path(base_directory).lexically_normal();
        
        // Check if the path is within the base directory
        auto relative = std::filesystem::relative(normalized_path, normalized_base);
        
        // Allow if paths are identical (relative path is ".")
        if (relative == ".") {
            return true;
        }
        
        return !relative.empty() && relative.native()[0] != '.';
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

std::string FileManager::sanitize_filename(const std::string& filename) {
    std::string sanitized = filename;
    
    // Remove or replace dangerous characters
    std::regex dangerous_chars(R"([<>:"/\\|?*])");
    sanitized = std::regex_replace(sanitized, dangerous_chars, "_");
    
    // Remove leading/trailing spaces and dots
    sanitized.erase(0, sanitized.find_first_not_of(" ."));
    sanitized.erase(sanitized.find_last_not_of(" .") + 1);
    
    // Ensure filename is not empty
    if (sanitized.empty()) {
        sanitized = "unnamed_file";
    }
    
    // Limit length
    if (sanitized.length() > 255) {
        sanitized = sanitized.substr(0, 255);
    }
    
    return sanitized;
}

} // namespace file
} // namespace netcopy

