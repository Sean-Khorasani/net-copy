#include "file/file_manager.h"
#include "common/chunk_size_manager.h"
#include "exceptions.h"
#include "crypto/sha3.h"
#include <algorithm>
#include <regex>
#include <vector>
#include <cctype>
#include <cwctype>
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace netcopy {
namespace file {

bool FileManager::exists(const std::string& path) {
    return std::filesystem::exists(std::filesystem::u8path(path));
}

bool FileManager::is_directory(const std::string& path) {
    return std::filesystem::is_directory(std::filesystem::u8path(path));
}

bool FileManager::is_regular_file(const std::string& path) {
    return std::filesystem::is_regular_file(std::filesystem::u8path(path));
}

uint64_t FileManager::file_size(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if (ec) {
        throw FileException("Failed to get file size for " + path + ": " + ec.message());
    }
    return size;
}

uint64_t FileManager::last_write_time(const std::string& path) {
    std::error_code ec;
    bool is_sym = std::filesystem::is_symlink(std::filesystem::u8path(path), ec);
    auto time = std::filesystem::last_write_time(std::filesystem::u8path(path), ec);
    if (ec) {
        if (is_sym) {
            return 0;
        }
        throw FileException("Failed to get last write time for " + path + ": " + ec.message());
    }
    
    // Convert to Unix timestamp
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

bool FileManager::create_directories(const std::string& path) {
    std::error_code ec;
    return std::filesystem::create_directories(std::filesystem::u8path(path), ec);
}

std::vector<FileManager::FileInfo> FileManager::list_directory(const std::string& path, bool recursive) {
    std::vector<FileInfo> files;
    
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(path))) {
                FileInfo info;
                info.path = entry.path().u8string();
                
                std::error_code ec;
                auto status = entry.symlink_status(ec);
                info.is_symlink = std::filesystem::is_symlink(status);
                if (info.is_symlink) {
                    info.symlink_target = read_symlink(info.path);
                    info.is_directory = false;
                    info.size = 0;
                    info.last_modified = last_write_time(info.path);
                } else {
                    info.is_directory = entry.is_directory();
                    if (!info.is_directory) {
                        info.size = file_size(info.path);
                        info.last_modified = last_write_time(info.path);
                    } else {
                        info.size = 0;
                        info.last_modified = 0;
                    }
                }
                info.permissions = get_permissions(info.path);
                
                files.push_back(info);
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::u8path(path))) {
                FileInfo info;
                info.path = entry.path().u8string();
                
                std::error_code ec;
                auto status = entry.symlink_status(ec);
                info.is_symlink = std::filesystem::is_symlink(status);
                if (info.is_symlink) {
                    info.symlink_target = read_symlink(info.path);
                    info.is_directory = false;
                    info.size = 0;
                    info.last_modified = last_write_time(info.path);
                } else {
                    info.is_directory = entry.is_directory();
                    if (!info.is_directory) {
                        info.size = file_size(info.path);
                        info.last_modified = last_write_time(info.path);
                    } else {
                        info.size = 0;
                        info.last_modified = 0;
                    }
                }
                info.permissions = get_permissions(info.path);
                
                files.push_back(info);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        throw FileException("Failed to list directory " + path + ": " + e.what());
    }
    
    return files;
}

// No changes needed for this function - it already uses the DEFAULT_CHUNK_SIZE defined in header
// The chunk size is already set to 256KB in the header (262144 bytes)

void FileManager::write_file_chunk(const std::string& path, uint64_t offset, const std::vector<uint8_t>& data, bool auto_create, bool truncate_on_zero) {
    // Create directory if it doesn't exist and auto_create is true
    auto dir = get_directory(path);
    if (auto_create && !dir.empty() && !exists(dir)) {
        create_directories(dir);
    }

#ifdef _WIN32
    {
    DWORD disposition = (offset == 0 && truncate_on_zero) ? CREATE_ALWAYS : OPEN_ALWAYS;
    HANDLE file_handle = CreateFileW(std::filesystem::u8path(path).wstring().c_str(),
                                     GENERIC_WRITE,
                                     FILE_SHARE_READ,
                                     nullptr,
                                     disposition,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
    if (file_handle == INVALID_HANDLE_VALUE) {
        throw FileException("Failed to open file for writing: " + path);
    }

    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file_handle, position, nullptr, FILE_BEGIN)) {
        CloseHandle(file_handle);
        throw FileException("Failed to seek to offset " + std::to_string(offset) + " in file: " + path);
    }

    size_t total_written = 0;
    while (total_written < data.size()) {
        DWORD bytes_to_write = static_cast<DWORD>(
            (std::min)(data.size() - total_written, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
        DWORD bytes_written = 0;
        if (!WriteFile(file_handle, data.data() + total_written, bytes_to_write, &bytes_written, nullptr)) {
            CloseHandle(file_handle);
            throw FileException("Failed to write data to file: " + path);
        }
        if (bytes_written == 0) {
            CloseHandle(file_handle);
            throw FileException("Failed to write data to file: " + path);
        }
        total_written += bytes_written;
    }

    CloseHandle(file_handle);
    return;
    }
#endif
    
    std::ios::openmode mode = std::ios::binary | std::ios::in | std::ios::out;
    
    // If starting from offset 0, truncate the file (non-resume mode)
    if (offset == 0 && truncate_on_zero) {
        mode |= std::ios::trunc;
    }
    
    std::fstream file(std::filesystem::u8path(path), mode);
    if (!file) {
        // File doesn't exist, create it
        file.open(std::filesystem::u8path(path), std::ios::binary | std::ios::out);
        if (!file) {
            throw FileException("Failed to create file: " + path);
        }
        file.close();
        file.open(std::filesystem::u8path(path), mode);
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

void FileManager::create_file(const std::string& path, uint64_t size, bool auto_create) {
    auto dir = get_directory(path);
    if (auto_create && !dir.empty() && !exists(dir)) {
        create_directories(dir);
    }
    
    std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
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
    std::filesystem::path p = std::filesystem::u8path(path);
    return p.lexically_normal().u8string();
}

std::string FileManager::get_filename(const std::string& path) {
    std::filesystem::path p = std::filesystem::u8path(path);
    std::string name = p.filename().u8string();
    if (name.empty() && p.has_parent_path()) {
        name = p.parent_path().filename().u8string();
    }
    return name;
}

std::string FileManager::get_directory(const std::string& path) {
    std::filesystem::path p = std::filesystem::u8path(path);
    return p.parent_path().u8string();
}

std::string FileManager::join_path(const std::string& base, const std::string& relative) {
    std::filesystem::path p = std::filesystem::u8path(base) / std::filesystem::u8path(relative);
    return p.u8string();
}

bool FileManager::is_path_safe(const std::string& path, const std::string& base_directory) {
    try {
        std::filesystem::path normalized_path = std::filesystem::u8path(path).lexically_normal();
        std::filesystem::path normalized_base = std::filesystem::u8path(base_directory).lexically_normal();
        
        std::filesystem::path relative;

#ifdef _WIN32
        // On Windows, paths are case-insensitive. We need to handle this for relative() to work as expected lexically.
        std::wstring path_w = normalized_path.wstring();
        std::wstring base_w = normalized_base.wstring();
        std::transform(path_w.begin(), path_w.end(), path_w.begin(), ::towlower);
        std::transform(base_w.begin(), base_w.end(), base_w.begin(), ::towlower);
        
        relative = std::filesystem::relative(std::filesystem::path(path_w), std::filesystem::path(base_w));
#else
        // Check if the path is within the base directory
        relative = std::filesystem::relative(normalized_path, normalized_base);
#endif
        
        // Allow if paths are identical (relative path is ".")
        if (relative == ".") {
            return true;
        }
        
        // Check if the relative path starts with ".." (escapes base directory)
        auto it = relative.begin();
        if (it != relative.end() && *it == "..") {
            return false;
        }
        
        return !relative.empty();
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}

std::string FileManager::sanitize_filename(const std::string& filename) {
    std::string sanitized = filename;

    // Remove or replace dangerous characters
    std::regex dangerous_chars(R"([<>:\/\\|?*])");
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

// Implementation of the missing read_file_chunk function
std::vector<uint8_t> FileManager::read_file_chunk(const std::string& path, uint64_t offset, size_t chunk_size) {
    if (chunk_size == 0) {
        chunk_size = DEFAULT_CHUNK_SIZE;
    }

    // Check if file exists
    if (!exists(path)) {
        throw FileException("File does not exist: " + path);
    }

    // Check if it's a regular file
    if (!is_regular_file(path)) {
        throw FileException("Path is not a regular file: " + path);
    }

    // Check if the file size is sufficient for reading at given offset
    uint64_t file_size_value = file_size(path);
    if (offset >= file_size_value) {
        // Return empty vector since we can't read from this offset
        return std::vector<uint8_t>();
    }

    // Determine how much to read (can be less than chunk_size if near end of file)
    size_t bytes_to_read = static_cast<size_t>(
        (std::min)(static_cast<uint64_t>(chunk_size), file_size_value - offset));

    // Read the data from the file
    std::vector<uint8_t> buffer(bytes_to_read);

#ifdef _WIN32
    {
    HANDLE file_handle = CreateFileW(std::filesystem::u8path(path).wstring().c_str(),
                                     GENERIC_READ,
                                     FILE_SHARE_READ,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
    if (file_handle == INVALID_HANDLE_VALUE) {
        throw FileException("Failed to open file for reading: " + path);
    }

    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(file_handle, position, nullptr, FILE_BEGIN)) {
        CloseHandle(file_handle);
        throw FileException("Failed to seek to offset " + std::to_string(offset) + " in file: " + path);
    }

    size_t total_read = 0;
    while (total_read < bytes_to_read) {
        DWORD request = static_cast<DWORD>(
            (std::min)(bytes_to_read - total_read, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
        DWORD bytes_read = 0;
        if (!ReadFile(file_handle, buffer.data() + total_read, request, &bytes_read, nullptr)) {
            CloseHandle(file_handle);
            throw FileException("Failed to read from file: " + path);
        }
        if (bytes_read == 0) {
            break;
        }
        total_read += bytes_read;
    }

    CloseHandle(file_handle);
    buffer.resize(total_read);
    return buffer;
    }
#endif

    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) {
        throw FileException("Failed to open file for reading: " + path);
    }

    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        throw FileException("Failed to seek in file: " + path);
    }

    file.read(reinterpret_cast<char*>(buffer.data()), bytes_to_read);
    std::streamsize bytes_read = file.gcount();
    if (bytes_read < 0) {
        throw FileException("Failed to read from file: " + path);
    }
    buffer.resize(static_cast<size_t>(bytes_read));
    if (buffer.size() != bytes_to_read && !file.eof()) {
        throw FileException("Failed to read from file: " + path);
    }

    return buffer;
}

std::vector<uint8_t> FileManager::compute_file_hash(const std::string& path) {
    if (!exists(path)) {
        throw FileException("File does not exist for hashing: " + path);
    }
    if (!is_regular_file(path)) {
        throw FileException("Path is not a regular file: " + path);
    }
    
    FileStream file;
    if (!file.open_read(path)) {
        throw FileException("Failed to open file for hashing: " + path);
    }
    
    crypto::Sha3Hasher hasher;
    std::vector<uint8_t> buffer(64 * 1024); // 64KB block size for hashing
    uint64_t offset = 0;
    while (true) {
        size_t bytes_read = file.read(offset, buffer.data(), buffer.size());
        if (bytes_read == 0) {
            break;
        }
        hasher.update(buffer.data(), bytes_read);
        offset += bytes_read;
    }
    file.close();
    return hasher.finalize();
}

std::vector<FileManager::BlockHash> FileManager::compute_block_hashes(const std::string& path, uint64_t block_size) {
    if (!exists(path)) {
        throw FileException("File does not exist for block hashing: " + path);
    }
    if (!is_regular_file(path)) {
        throw FileException("Path is not a regular file: " + path);
    }
    
    FileStream file;
    if (!file.open_read(path)) {
        throw FileException("Failed to open file for block hashing: " + path);
    }
    
    std::vector<BlockHash> hashes;
    std::vector<uint8_t> buffer(block_size);
    uint64_t offset = 0;
    while (true) {
        size_t bytes_read = file.read(offset, buffer.data(), buffer.size());
        if (bytes_read == 0) {
            break;
        }
        
        crypto::Sha3Hasher hasher;
        hasher.update(buffer.data(), bytes_read);
        
        BlockHash bh;
        bh.offset = offset;
        bh.hash = hasher.finalize();
        hashes.push_back(std::move(bh));
        
        offset += bytes_read;
    }
    file.close();
    return hashes;
}

uint32_t FileManager::get_permissions(const std::string& path) {
    std::error_code ec;
    auto status = std::filesystem::symlink_status(std::filesystem::u8path(path), ec);
    if (ec) {
        return 0;
    }
    return static_cast<uint32_t>(status.permissions());
}

void FileManager::set_permissions(const std::string& path, uint32_t permissions) {
    std::error_code ec;
    std::filesystem::permissions(std::filesystem::u8path(path), static_cast<std::filesystem::perms>(permissions), ec);
}

bool FileManager::is_symlink(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::u8path(path), ec);
}

std::string FileManager::read_symlink(const std::string& path) {
    std::error_code ec;
    auto target = std::filesystem::read_symlink(std::filesystem::u8path(path), ec);
    if (ec) {
        return "";
    }
    return target.u8string();
}

bool FileManager::create_symlink(const std::string& target, const std::string& symlink_path) {
    std::error_code ec;
    bool is_dir = false;
    if (std::filesystem::exists(std::filesystem::u8path(target), ec)) {
        is_dir = std::filesystem::is_directory(std::filesystem::u8path(target), ec);
    }
    
    if (is_dir) {
        std::filesystem::create_directory_symlink(std::filesystem::u8path(target), std::filesystem::u8path(symlink_path), ec);
    } else {
        std::filesystem::create_symlink(std::filesystem::u8path(target), std::filesystem::u8path(symlink_path), ec);
    }
    
    if (ec) {
        // Fallback: If symlink creation fails (e.g. privilege error), write a placeholder file
        // containing the target path, so the transfer doesn't fail completely.
        std::ofstream placeholder(std::filesystem::u8path(symlink_path));
        if (placeholder.is_open()) {
            placeholder << target;
            placeholder.close();
            return true;
        }
        return false;
    }
    return true;
}

// FileStream implementation
FileStream::FileStream()
#ifdef _WIN32
    : file_handle_(nullptr)
#else
    : fd_(-1)
#endif
{}

FileStream::~FileStream() {
    close();
}

// Move constructor
FileStream::FileStream(FileStream&& other) noexcept
#ifdef _WIN32
    : file_handle_(other.file_handle_), path_(std::move(other.path_))
#else
    : fd_(other.fd_), path_(std::move(other.path_))
#endif
{
#ifdef _WIN32
    other.file_handle_ = nullptr;
#else
    other.fd_ = -1;
#endif
}

// Move assignment operator
FileStream& FileStream::operator=(FileStream&& other) noexcept {
    if (this != &other) {
        close();
#ifdef _WIN32
        file_handle_ = other.file_handle_;
        other.file_handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        path_ = std::move(other.path_);
    }
    return *this;
}

bool FileStream::open_read(const std::string& path) {
    close();
    path_ = path;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    int retry_count = 5;
    int delay_ms = 100;
    while (retry_count > 0) {
        handle = CreateFileW(std::filesystem::u8path(path).wstring().c_str(),
                             GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() == ERROR_SHARING_VIOLATION) {
            retry_count--;
            if (retry_count > 0) {
                Sleep(delay_ms);
                delay_ms *= 2;
                continue;
            }
        }
        break;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    file_handle_ = handle;
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    fd_ = fd;
#endif
    return true;
}

bool FileStream::open_write(const std::string& path, bool truncate_on_zero, bool auto_create) {
    close();
    path_ = path;
    
    if (auto_create) {
        auto dir = FileManager::get_directory(path);
        if (!dir.empty() && !FileManager::exists(dir)) {
            FileManager::create_directories(dir);
        }
    }
    
#ifdef _WIN32
    DWORD disposition = truncate_on_zero ? CREATE_ALWAYS : OPEN_ALWAYS;
    HANDLE handle = INVALID_HANDLE_VALUE;
    int retry_count = 5;
    int delay_ms = 100;
    while (retry_count > 0) {
        handle = CreateFileW(std::filesystem::u8path(path).wstring().c_str(),
                             GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr,
                             disposition,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            break;
        }
        if (GetLastError() == ERROR_SHARING_VIOLATION) {
            retry_count--;
            if (retry_count > 0) {
                Sleep(delay_ms);
                delay_ms *= 2;
                continue;
            }
        }
        break;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    file_handle_ = handle;
#else
    int flags = O_WRONLY | O_CREAT;
    if (truncate_on_zero) {
        flags |= O_TRUNC;
    }
    int fd = open(path.c_str(), flags, 0644);
    if (fd < 0) {
        return false;
    }
    fd_ = fd;
#endif
    return true;
}

size_t FileStream::read(uint64_t offset, uint8_t* buffer, size_t size) {
    if (!is_open()) return 0;
#ifdef _WIN32
    HANDLE handle = static_cast<HANDLE>(file_handle_);
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
        throw FileException("FileStream read failed to seek: " + path_);
    }
    
    DWORD bytes_read = 0;
    if (!ReadFile(handle, buffer, static_cast<DWORD>(size), &bytes_read, nullptr)) {
        throw FileException("FileStream read failed: " + path_);
    }
    return static_cast<size_t>(bytes_read);
#else
    ssize_t res = pread(fd_, buffer, size, static_cast<off_t>(offset));
    if (res < 0) {
        throw FileException("FileStream read failed: " + path_);
    }
    return static_cast<size_t>(res);
#endif
}

void FileStream::write(uint64_t offset, const uint8_t* data, size_t size) {
    if (!is_open()) return;
#ifdef _WIN32
    HANDLE handle = static_cast<HANDLE>(file_handle_);
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) {
        throw FileException("FileStream write failed to seek: " + path_);
    }
    
    DWORD bytes_written = 0;
    if (!WriteFile(handle, data, static_cast<DWORD>(size), &bytes_written, nullptr)) {
        throw FileException("FileStream write failed: " + path_);
    }
#else
    ssize_t res = pwrite(fd_, data, size, static_cast<off_t>(offset));
    if (res < 0) {
        throw FileException("FileStream write failed: " + path_);
    }
#endif
}

void FileStream::close() {
#ifdef _WIN32
    if (file_handle_ != nullptr && file_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(file_handle_));
        file_handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

bool FileStream::is_open() const {
#ifdef _WIN32
    return file_handle_ != nullptr && file_handle_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
}

} // namespace file
} // namespace netcopy
