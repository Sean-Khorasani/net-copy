#include "crypto/xor_cipher.h"
#include <random>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace netcopy {
namespace crypto {

XorCipher::XorCipher(const Key& key) 
    : base_key_(key), current_key_(key), round_counter_(0) {
}

std::vector<uint8_t> XorCipher::process(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> result = data;
    
    size_t pos = 0;
    while (pos < result.size()) {
        size_t chunk_size = std::min(CHUNK_SIZE, result.size() - pos);
        
        // XOR with current key
        for (size_t i = 0; i < chunk_size; ++i) {
            result[pos + i] ^= current_key_[i % KEY_SIZE];
        }
        
        pos += chunk_size;
        
        // Update key for next chunk if there's more data
        if (pos < result.size()) {
            update_key();
        }
    }
    
    return result;
}

void XorCipher::process_chunk(std::vector<uint8_t>& data) {
    // XOR with current key
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= current_key_[i % KEY_SIZE];
    }
    
    // Update key for next chunk
    update_key();
}

void XorCipher::reset() {
    current_key_ = base_key_;
    round_counter_ = 0;
}

XorCipher::Key XorCipher::generate_key() {
    Key key;
    
    // Use high-resolution clock for better randomness
    auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937_64 gen(seed);
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    
    for (auto& byte : key) {
        byte = dist(gen);
    }
    
    return key;
}

XorCipher::Key XorCipher::derive_key(const std::string& password) {
    Key key = {};
    
    // Simple key derivation: hash password multiple times
    std::vector<uint8_t> data(password.begin(), password.end());
    
    for (int round = 0; round < 1000; ++round) {
        // Simple hash mixing
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] ^= static_cast<uint8_t>((round * 31 + i * 17) & 0xFF);
        }
        
        // Mix into key
        for (size_t i = 0; i < data.size() && i < KEY_SIZE; ++i) {
            key[i] ^= data[i];
        }
    }
    
    // Ensure key is not all zeros
    if (std::all_of(key.begin(), key.end(), [](uint8_t b) { return b == 0; })) {
        std::fill(key.begin(), key.end(), 0x5A); // Default non-zero pattern
    }
    
    return key;
}

void XorCipher::update_key() {
    ++round_counter_;
    
    // Rolling key update algorithm
    for (size_t i = 0; i < KEY_SIZE; ++i) {
        current_key_[i] ^= static_cast<uint8_t>((round_counter_ * 31 + i * 17) & 0xFF);
        current_key_[i] = static_cast<uint8_t>((current_key_[i] << 1) | (current_key_[i] >> 7)); // Rotate left
    }
}

} // namespace crypto
} // namespace netcopy