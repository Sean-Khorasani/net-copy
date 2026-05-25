#include "common/compression.h"
#include "common/fast_mem.h"
#if __has_include(<lz4.h>)
#include <lz4.h>
#define HAS_LZ4
#endif
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace netcopy {
namespace common {

namespace {
bool has_extension(const std::string& path, const std::vector<std::string>& exts) {
    std::filesystem::path p = std::filesystem::u8path(path);
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}
} // namespace

bool is_compressible(const std::string& path) {
#ifndef HAS_LZ4
    return false;
#endif
    static const std::vector<std::string> non_compressible = {
        ".jpg", ".jpeg", ".png", ".gif", ".mp3", ".mp4", ".avi",
        ".zip", ".gz", ".bz2", ".rar", ".7z", ".lz4", ".pdf",
        ".mpg", ".mpeg", ".ogg", ".flac"
    };
    return !has_extension(path, non_compressible);
}

std::vector<uint8_t> compress_buffer(const std::vector<uint8_t>& data) {
    return compress_buffer(data.data(), data.size());
}

std::vector<uint8_t> compress_buffer(const uint8_t* data, size_t size) {
#ifdef HAS_LZ4
    int max_dst = LZ4_compressBound(static_cast<int>(size));
    std::vector<uint8_t> out(max_dst);
    int compressed_size = LZ4_compress_default(
        reinterpret_cast<const char*>(data),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(size),
        max_dst);
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    out.resize(compressed_size);
    return out;
#else
    std::vector<uint8_t> copy(size);
    if (size > 0) {
        fast_mem::fast_memcpy(copy.data(), data, size);
    }
    return copy;
#endif
}

std::vector<uint8_t> decompress_buffer(const std::vector<uint8_t>& data, size_t original_size) {
    return decompress_buffer(data.data(), data.size(), original_size);
}

std::vector<uint8_t> decompress_buffer(const uint8_t* data, size_t size, size_t original_size) {
#ifdef HAS_LZ4
    std::vector<uint8_t> out(original_size);
    int decompressed = LZ4_decompress_safe(
        reinterpret_cast<const char*>(data),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(size),
        static_cast<int>(original_size));
    if (decompressed < 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    out.resize(decompressed);
    return out;
#else
    if (size != original_size) {
         throw std::runtime_error("LZ4 decompression failed: LZ4 not available but data seems compressed");
    }
    std::vector<uint8_t> copy(size);
    if (size > 0) {
        fast_mem::fast_memcpy(copy.data(), data, size);
    }
    return copy;
#endif
}

} // namespace common
} // namespace netcopy
