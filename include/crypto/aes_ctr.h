#pragma once

#include "crypto/chacha20_poly1305.h"
#include <array>
#include <vector>
#include <cstdint>

namespace netcopy {
namespace crypto {

class AesCtr {
public:
    static constexpr size_t KEY_SIZE = 32;  // AES-256
    static constexpr size_t BLOCK_SIZE = 16;
    static constexpr size_t IV_SIZE = 16;
    
    using Key = std::array<uint8_t, KEY_SIZE>;
    using IV = std::array<uint8_t, IV_SIZE>;
    
    explicit AesCtr(const Key& key);
    ~AesCtr();
    
    // Process data with CTR mode (encryption and decryption are the same)
    std::vector<uint8_t> process(const std::vector<uint8_t>& data, const IV& iv);
    
    // Reset internal state
    void reset();
    
    // Static utility functions
    static Key generate_key();
    static IV generate_iv();
    static Key derive_key(const std::string& password);
    
    // Hardware acceleration detection
    static bool is_aes_ni_supported();
    static bool is_simd_supported();
    
    // Performance information
    static std::string get_acceleration_info();
    static std::string get_detailed_acceleration_info();
    bool is_using_hardware_acceleration() const;

private:
    Key key_;
    bool use_aes_ni_;
    bool use_simd_;
    
    // Internal AES implementation
    void expand_key();
    void encrypt_block_software(const uint8_t* plaintext, uint8_t* ciphertext);
    void encrypt_block_aes_ni(const uint8_t* plaintext, uint8_t* ciphertext);
    void encrypt_blocks_simd(const uint8_t* plaintext, uint8_t* ciphertext, size_t num_blocks);
    
    // Key schedule storage
    std::array<uint8_t, 240> expanded_key_;  // AES-256 expanded key
};

} // namespace crypto
} // namespace netcopy