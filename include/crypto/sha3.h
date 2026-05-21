#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace netcopy {
namespace crypto {

// SHA3-256 (FIPS 202 / Keccak)
std::vector<uint8_t> sha3_256(const uint8_t* data, size_t len);
std::vector<uint8_t> sha3_256(const std::vector<uint8_t>& data);
std::vector<uint8_t> sha3_256(const std::string& data);

// HMAC-SHA3-256
std::vector<uint8_t> hmac_sha3_256(const std::vector<uint8_t>& key,
                                    const std::vector<uint8_t>& data);

// PBKDF2-HMAC-SHA3-256
// Returns derived key of `output_len` bytes.
std::vector<uint8_t> pbkdf2_sha3_256(const std::string& password,
                                      const std::vector<uint8_t>& salt,
                                      int iterations,
                                      size_t output_len);

// Hex encode/decode helpers
std::string bytes_to_hex(const std::vector<uint8_t>& data);
std::vector<uint8_t> hex_to_bytes(const std::string& hex);

// Base64 encode/decode helpers
std::string base64_encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64_decode(const std::string& b64);

// Generate cryptographically random bytes
std::vector<uint8_t> random_bytes(size_t count);

} // namespace crypto
} // namespace netcopy
