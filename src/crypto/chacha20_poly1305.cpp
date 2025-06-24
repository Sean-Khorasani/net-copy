#include "crypto/chacha20_poly1305.h"
#include "exceptions.h"
#include <random>
#include <cstring>
#include <algorithm>

namespace netcopy {
namespace crypto {

// ChaCha20 implementation
class ChaCha20 {
public:
    static constexpr size_t BLOCK_SIZE = 64;
    
    ChaCha20(const ChaCha20Poly1305::Key& key, const ChaCha20Poly1305::Nonce& nonce, uint32_t counter = 0) {
        // Initialize state
        state_[0] = 0x61707865;
        state_[1] = 0x3320646e;
        state_[2] = 0x79622d32;
        state_[3] = 0x6b206574;
        
        // Key
        std::memcpy(&state_[4], key.data(), 32);
        
        // Counter
        state_[12] = counter;
        
        // Nonce
        std::memcpy(&state_[13], nonce.data(), 12);
    }
    
    void encrypt(uint8_t* data, size_t length) {
        size_t offset = 0;
        while (offset < length) {
            uint8_t keystream[BLOCK_SIZE];
            generate_keystream(keystream);
            
            size_t chunk_size = std::min(length - offset, BLOCK_SIZE);
            for (size_t i = 0; i < chunk_size; ++i) {
                data[offset + i] ^= keystream[i];
            }
            
            offset += chunk_size;
            state_[12]++; // Increment counter
        }
    }

private:
    uint32_t state_[16];
    
    void generate_keystream(uint8_t* output) {
        uint32_t working_state[16];
        std::memcpy(working_state, state_, sizeof(state_));
        
        // 20 rounds
        for (int i = 0; i < 10; ++i) {
            quarter_round(working_state[0], working_state[4], working_state[8], working_state[12]);
            quarter_round(working_state[1], working_state[5], working_state[9], working_state[13]);
            quarter_round(working_state[2], working_state[6], working_state[10], working_state[14]);
            quarter_round(working_state[3], working_state[7], working_state[11], working_state[15]);
            quarter_round(working_state[0], working_state[5], working_state[10], working_state[15]);
            quarter_round(working_state[1], working_state[6], working_state[11], working_state[12]);
            quarter_round(working_state[2], working_state[7], working_state[8], working_state[13]);
            quarter_round(working_state[3], working_state[4], working_state[9], working_state[14]);
        }
        
        // Add original state
        for (int i = 0; i < 16; ++i) {
            working_state[i] += state_[i];
        }
        
        // Convert to bytes (little-endian)
        for (int i = 0; i < 16; ++i) {
            output[i * 4] = working_state[i] & 0xff;
            output[i * 4 + 1] = (working_state[i] >> 8) & 0xff;
            output[i * 4 + 2] = (working_state[i] >> 16) & 0xff;
            output[i * 4 + 3] = (working_state[i] >> 24) & 0xff;
        }
    }
    
    static void quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
        a += b; d ^= a; d = rotl(d, 16);
        c += d; b ^= c; b = rotl(b, 12);
        a += b; d ^= a; d = rotl(d, 8);
        c += d; b ^= c; b = rotl(b, 7);
    }
    
    static uint32_t rotl(uint32_t value, int shift) {
        return (value << shift) | (value >> (32 - shift));
    }
};

// Poly1305 implementation
class Poly1305 {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t TAG_SIZE = 16;
    
    Poly1305(const uint8_t* key) {
        // Initialize r and s from key
        r_[0] = (load32(key) & 0x3ffffff);
        r_[1] = (load32(key + 3) >> 2) & 0x3ffff03;
        r_[2] = (load32(key + 6) >> 4) & 0x3ffc0ff;
        r_[3] = (load32(key + 9) >> 6) & 0x3f03fff;
        r_[4] = (load32(key + 12) >> 8) & 0x00fffff;
        
        s_[0] = load32(key + 16);
        s_[1] = load32(key + 20);
        s_[2] = load32(key + 24);
        s_[3] = load32(key + 28);
        
        // Initialize accumulator
        std::fill(h_, h_ + 5, 0);
    }
    
    void update(const uint8_t* data, size_t length) {
        size_t offset = 0;
        while (offset < length) {
            size_t chunk_size = std::min(length - offset, size_t(16));
            
            uint32_t c[5] = {0};
            for (size_t i = 0; i < chunk_size; ++i) {
                c[i / 4] |= uint32_t(data[offset + i]) << (8 * (i % 4));
            }
            c[chunk_size / 4] |= 1 << (8 * (chunk_size % 4));
            
            // Add to accumulator
            uint64_t carry = 0;
            for (int i = 0; i < 5; ++i) {
                carry += uint64_t(h_[i]) + c[i];
                h_[i] = carry & 0xffffffff;
                carry >>= 32;
            }
            
            // Multiply by r
            multiply_by_r();
            
            offset += chunk_size;
        }
    }
    
    void finalize(uint8_t* tag) {
        // Final reduction
        uint64_t carry = 0;
        for (int i = 0; i < 5; ++i) {
            carry += h_[i];
            h_[i] = carry & 0xffffffff;
            carry >>= 32;
        }
        
        // Add s
        carry = 0;
        for (int i = 0; i < 4; ++i) {
            carry += uint64_t(h_[i]) + s_[i];
            store32(tag + i * 4, carry & 0xffffffff);
            carry >>= 32;
        }
    }

private:
    uint32_t r_[5];
    uint32_t s_[4];
    uint32_t h_[5];
    
    static uint32_t load32(const uint8_t* data) {
        return uint32_t(data[0]) | (uint32_t(data[1]) << 8) | 
               (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
    }
    
    static void store32(uint8_t* data, uint32_t value) {
        data[0] = value & 0xff;
        data[1] = (value >> 8) & 0xff;
        data[2] = (value >> 16) & 0xff;
        data[3] = (value >> 24) & 0xff;
    }
    
    void multiply_by_r() {
        uint64_t d[5] = {0};
        
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 5; ++j) {
                d[i] += uint64_t(h_[j]) * r_[(i - j + 5) % 5];
                if (i - j < 0) d[i] += uint64_t(h_[j]) * r_[i - j + 5] * 5;
            }
        }
        
        uint64_t carry = 0;
        for (int i = 0; i < 5; ++i) {
            carry += d[i];
            h_[i] = carry & 0xffffffff;
            carry >>= 32;
        }
    }
};

// ChaCha20Poly1305 implementation
class ChaCha20Poly1305::Impl {
public:
    Key key_;
    
    Impl(const Key& key) : key_(key) {}
};

ChaCha20Poly1305::ChaCha20Poly1305(const Key& key) 
    : pimpl_(std::make_unique<Impl>(key)) {}

ChaCha20Poly1305::~ChaCha20Poly1305() = default;

std::vector<uint8_t> ChaCha20Poly1305::encrypt(const std::vector<uint8_t>& plaintext, 
                                               const Nonce& nonce,
                                               const std::vector<uint8_t>& additional_data) {
    // Generate Poly1305 key using ChaCha20
    uint8_t poly_key[32] = {0};
    ChaCha20 chacha_for_key(pimpl_->key_, nonce, 0);
    chacha_for_key.encrypt(poly_key, 32);
    
    // Encrypt plaintext
    std::vector<uint8_t> ciphertext = plaintext;
    ChaCha20 chacha_for_data(pimpl_->key_, nonce, 1);
    chacha_for_data.encrypt(ciphertext.data(), ciphertext.size());
    
    // Compute authentication tag
    Poly1305 poly(poly_key);
    poly.update(additional_data.data(), additional_data.size());
    poly.update(ciphertext.data(), ciphertext.size());
    
    // Add lengths
    uint8_t lengths[16] = {0};
    uint64_t ad_len = additional_data.size();
    uint64_t ct_len = ciphertext.size();
    std::memcpy(lengths, &ad_len, 8);
    std::memcpy(lengths + 8, &ct_len, 8);
    poly.update(lengths, 16);
    
    uint8_t tag[TAG_SIZE];
    poly.finalize(tag);
    
    // Append tag to ciphertext
    ciphertext.insert(ciphertext.end(), tag, tag + TAG_SIZE);
    
    return ciphertext;
}

std::vector<uint8_t> ChaCha20Poly1305::decrypt(const std::vector<uint8_t>& ciphertext,
                                               const Nonce& nonce,
                                               const Tag& tag,
                                               const std::vector<uint8_t>& additional_data) {
    if (ciphertext.size() < TAG_SIZE) {
        throw CryptoException("Ciphertext too short");
    }
    
    // Extract actual ciphertext (without tag)
    std::vector<uint8_t> actual_ciphertext(ciphertext.begin(), ciphertext.end() - TAG_SIZE);
    
    // Generate Poly1305 key
    uint8_t poly_key[32] = {0};
    ChaCha20 chacha_for_key(pimpl_->key_, nonce, 0);
    chacha_for_key.encrypt(poly_key, 32);
    
    // Verify authentication tag
    Poly1305 poly(poly_key);
    poly.update(additional_data.data(), additional_data.size());
    poly.update(actual_ciphertext.data(), actual_ciphertext.size());
    
    uint8_t lengths[16] = {0};
    uint64_t ad_len = additional_data.size();
    uint64_t ct_len = actual_ciphertext.size();
    std::memcpy(lengths, &ad_len, 8);
    std::memcpy(lengths + 8, &ct_len, 8);
    poly.update(lengths, 16);
    
    uint8_t computed_tag[TAG_SIZE];
    poly.finalize(computed_tag);
    
    if (std::memcmp(computed_tag, tag.data(), TAG_SIZE) != 0) {
        throw CryptoException("Authentication failed");
    }
    
    // Decrypt
    std::vector<uint8_t> plaintext = actual_ciphertext;
    ChaCha20 chacha_for_data(pimpl_->key_, nonce, 1);
    chacha_for_data.encrypt(plaintext.data(), plaintext.size());
    
    return plaintext;
}

ChaCha20Poly1305::Key ChaCha20Poly1305::generate_key() {
    Key key;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (auto& byte : key) {
        byte = dis(gen);
    }
    
    return key;
}

ChaCha20Poly1305::Nonce ChaCha20Poly1305::generate_nonce() {
    Nonce nonce;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (auto& byte : nonce) {
        byte = dis(gen);
    }
    
    return nonce;
}

ChaCha20Poly1305::Key ChaCha20Poly1305::derive_key(const std::string& password, 
                                                   const std::vector<uint8_t>& salt,
                                                   int iterations) {
    // Simple PBKDF2 implementation (for production, use a proper crypto library)
    Key key;
    
    // This is a simplified implementation - in production, use OpenSSL or similar
    std::hash<std::string> hasher;
    size_t hash_value = hasher(password + std::string(salt.begin(), salt.end()));
    
    for (int i = 0; i < iterations; ++i) {
        hash_value = hasher(std::to_string(hash_value));
    }
    
    // Fill key with derived data
    for (size_t i = 0; i < KEY_SIZE; ++i) {
        key[i] = (hash_value >> (i % 8)) & 0xff;
        if (i % 8 == 7) {
            hash_value = hasher(std::to_string(hash_value));
        }
    }
    
    return key;
}

} // namespace crypto
} // namespace netcopy

