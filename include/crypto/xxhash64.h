#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

namespace netcopy {
namespace crypto {

// ---------------------------------------------------------------------------
// xxHash64  –  extremely fast non-cryptographic 64-bit hash.
// Designed for file checksums / block comparison where collision-resistance
// is NOT a requirement.  Throughput on modern x86-64: ~15-20 GB/s.
//
// This is a clean-room implementation of the canonical XXH64 algorithm
// (Yann Collet, BSD license).
// ---------------------------------------------------------------------------

// One-shot hash of a buffer.
uint64_t xxhash64(const void* data, size_t len, uint64_t seed = 0);

// One-shot hash returning raw 8-byte vector (convenience for existing APIs).
inline std::vector<uint8_t> xxhash64_bytes(const void* data, size_t len, uint64_t seed = 0) {
    uint64_t h = xxhash64(data, len, seed);
    std::vector<uint8_t> out(8);
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(h >> (i * 8));
    return out;
}

// ---------------------------------------------------------------------------
// Streaming hasher –  drop-in replacement for Sha3Hasher in FileManager.
// ---------------------------------------------------------------------------
class XxHash64Hasher {
public:
    XxHash64Hasher(uint64_t seed = 0);

    void update(const uint8_t* data, size_t len);
    void update(const std::vector<uint8_t>& data) { update(data.data(), data.size()); }

    // Finalise and return 8-byte hash vector.
    std::vector<uint8_t> finalize();

private:
    uint64_t state_[4]{};    // v1..v4 accumulators
    uint8_t  buffer_[32]{};  // stripe buffer
    size_t   buffer_len_ = 0;
    uint64_t total_len_ = 0;
    uint64_t seed_;
};

} // namespace crypto
} // namespace netcopy
