#include <file/file_manager.h>
#include <logging/logger.h>
#include <exceptions.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

std::string FileManager::get_directory(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

uint64_t FileManager::file_size(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        throw FileException("Failed to get file size for " + path + ": " + ec.message());
    }
    return size;
}

void FileManager::create_directories(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw FileException("Failed to create directories: " + path + ", error: " + ec.message());
    }
}

void FileManager::write_file_chunk(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data, bool auto_create) {
    // Create directory if it doesn't exist and auto_create is true
    auto dir = get_directory(path);
    if (auto_create && !dir.empty() && !exists(dir)) {
        create_directories(dir);
    }
    
    std::ios::openmode mode = std::ios::binary | std::ios::in | std::ios::out;
    
    // If starting from offset 0, truncate the file (non-resume mode)
    if (offset == 0) {
        mode |= std::ios::trunc;
    }
    
    std::fstream file(path, mode);
    if (!file) {
        throw FileException("Failed to open file for writing: " + path);
    }
    
    // Seek to the correct position
    file.seekg(offset);
    
    // Write data
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    
    // Check for write errors
    if (file.bad()) {
        throw FileException("Failed to write to file: " + path);
    }
}

void FileManager::create_file(const std::string& path, uint64_t size, bool auto_create) {
    auto dir = get_directory(path);
    if (auto_create && !dir.empty() && !exists(dir)) {
        create_directories(dir);
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw FileException("Failed to create file: " + path);
    }
    
    // Resize the file
    file.seekp(size - 1);
    file.put('\0');
    
    file.close();
}

std::vector<uint8_t> FileManager::read_file_chunk(const std::string& path, uint64_t offset, size_t length) {
    // Check if file exists
    if (!exists(path)) {
        throw FileException("File does not exist: " + path);
    }

    // Check if it's a regular file
    if (!is_regular_file(path)) {
        throw FileException("Path is not a regular file: " + path);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw FileException("Failed to open file for reading: " + path);
    }

    // Seek to the correct position
    file.seekg(offset);

    // Read data
    std::vector<uint8_t> data(length);
    file.read(reinterpret_cast<char*>(data.data()), length);

    // Check for read errors
    if (file.bad()) {
        throw FileException("Failed to read from file: " + path);
    }

    return data;
}

} // namespace file
} // namespace netcopy
