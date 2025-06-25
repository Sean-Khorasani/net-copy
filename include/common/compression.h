#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace netcopy {
namespace common {

bool is_compressible(const std::string& path);
std::vector<uint8_t> compress_buffer(const std::vector<uint8_t>& data);
std::vector<uint8_t> decompress_buffer(const std::vector<uint8_t>& data, size_t original_size);

} // namespace common
} // namespace netcopy
