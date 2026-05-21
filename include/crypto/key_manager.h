#pragma once
#include "crypto/mlkem.h"
#include <string>
#include <vector>

namespace netcopy {
namespace crypto {

// PEM-like key file format for ML-KEM keys.
// Plain private key:
//   -----BEGIN ML-KEM-768 PRIVATE KEY-----
//   <base64>
//   -----END ML-KEM-768 PRIVATE KEY-----
//
// Encrypted private key (passphrase != ""):
//   -----BEGIN ENCRYPTED ML-KEM-768 PRIVATE KEY-----
//   KDF: PBKDF2-SHA3-256
//   Iterations: 200000
//   Salt: <hex>
//   IV: <hex>
//   <base64 of AES-256-CTR(key_bytes) || HMAC-SHA3-256>
//   -----END ENCRYPTED ML-KEM-768 PRIVATE KEY-----
//
// Public key:
//   -----BEGIN ML-KEM-768 PUBLIC KEY-----
//   <base64>
//   -----END ML-KEM-768 PUBLIC KEY-----

void save_public_key(const std::string& path,
                     const std::vector<uint8_t>& key,
                     MlKemLevel level);

void save_private_key(const std::string& path,
                      const std::vector<uint8_t>& key,
                      MlKemLevel level,
                      const std::string& passphrase = "");

// Returns key bytes; sets level_out
std::vector<uint8_t> load_public_key(const std::string& path, MlKemLevel& level_out);
std::vector<uint8_t> load_private_key(const std::string& path, MlKemLevel& level_out,
                                       const std::string& passphrase = "");

// Reads a private key file and returns only the public key portion
// (first public_key_size bytes of an ML-KEM secret key are the public key)
std::vector<uint8_t> extract_public_key_from_private(const std::string& path,
                                                      MlKemLevel& level_out,
                                                      const std::string& passphrase = "");

} // namespace crypto
} // namespace netcopy
