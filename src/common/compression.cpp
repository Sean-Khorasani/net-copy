#include "common/compression.h"
#include <lz4.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace netcopy {
namespace common {

namespace {
bool has_extension(const std::string& path, const std::vector<std::string>& exts) {
    std::filesystem::path p(path);
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}
} // namespace

bool is_compressible(const std::string& path) {
    static const std::vector<std::string> non_compressible = {
        ".jpg", ".jpeg", ".png", ".gif", ".mp3", ".mp4", ".avi",
        ".zip", ".gz", ".bz2", ".rar", ".7z", ".lz4", ".pdf",
        ".mpg", ".mpeg", ".ogg", ".flac"
    };
    return !has_extension(path, non_compressible);
}

std::vector<uint8_t> compress_buffer(const std::vector<uint8_t>& data) {
    int max_dst = LZ4_compressBound(static_cast<int>(data.size()));
    std::vector<uint8_t> out(max_dst);
    int compressed_size = LZ4_compress_default(
        reinterpret_cast<const char*>(data.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(data.size()),
        max_dst);
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    out.resize(compressed_size);
    return out;
}

std::vector<uint8_t> decompress_buffer(const std::vector<uint8_t>& data, size_t original_size) {
    std::vector<uint8_t> out(original_size);
    int decompressed = LZ4_decompress_safe(
        reinterpret_cast<const char*>(data.data()),
        reinterpret_cast<char*>(out.data()),
        static_cast<int>(data.size()),
        static_cast<int>(original_size));
    if (decompressed < 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    out.resize(decompressed);
    return out;
}

} // namespace common
} // namespace netcopy
