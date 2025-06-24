#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <memory>

namespace netcopy {
namespace crypto {

enum class SecurityLevel : uint8_t {
    HIGH = 0,        // ChaCha20-Poly1305 AEAD (secure but slower)
    FAST = 1,        // XOR with rolling key (fast but less secure)
    AES = 2,         // AES-CTR with hardware acceleration (balanced security/performance)
    AES_256_GCM = 3  // AES-256-GCM with GPU acceleration (fastest with high security)
};

class ChaCha20Poly1305 {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 12;
    static constexpr size_t TAG_SIZE = 16;

    using Key = std::array<uint8_t, KEY_SIZE>;
    using Nonce = std::array<uint8_t, NONCE_SIZE>;
    using Tag = std::array<uint8_t, TAG_SIZE>;

    ChaCha20Poly1305(const Key& key);
    ~ChaCha20Poly1305();

    // Encrypt data with authentication
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, 
                                const Nonce& nonce,
                                const std::vector<uint8_t>& additional_data = {});

    // Decrypt and verify data
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext,
                                const Nonce& nonce,
                                const Tag& tag,
                                const std::vector<uint8_t>& additional_data = {});

    // Generate random key
    static Key generate_key();
    
    // Generate random nonce
    static Nonce generate_nonce();

    // Derive key from password using PBKDF2
    static Key derive_key(const std::string& password, 
                         const std::vector<uint8_t>& salt,
                         int iterations = 100000);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace crypto
} // namespace netcopy

