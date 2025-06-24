#pragma once

#include <vector>
#include <cstdint>
#include <array>
#include <string>

namespace netcopy {
namespace crypto {

class XorCipher {
public:
    static constexpr size_t KEY_SIZE = 32;
    static constexpr size_t CHUNK_SIZE = 1024;

    using Key = std::array<uint8_t, KEY_SIZE>;

    explicit XorCipher(const Key& key);

    // Encrypt/decrypt data (XOR is symmetric)
    std::vector<uint8_t> process(const std::vector<uint8_t>& data);
    
    // Process data in chunks for streaming
    void process_chunk(std::vector<uint8_t>& data);

    // Reset cipher state for new data stream
    void reset();

    // Generate random key
    static Key generate_key();
    
    // Derive key from password (simple hash-based derivation)
    static Key derive_key(const std::string& password);

private:
    Key base_key_;
    Key current_key_;
    size_t round_counter_;

    // Update key for next round (rolling key mechanism)
    void update_key();
};

} // namespace crypto
} // namespace netcopy