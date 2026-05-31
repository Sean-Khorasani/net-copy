// xxHash64  –  canonical XXH64 implementation (Yann Collet, BSD).
// Optimised for 64-bit little-endian platforms with unaligned access.
#include "crypto/xxhash64.h"

#ifdef _MSC_VER
#  include <stdlib.h>
#  define XXH_rotl64(x,r)  _rotl64(x,r)
#else
#  define XXH_rotl64(x,r)  (((x) << (r)) | ((x) >> (64 - (r))))
#endif

namespace netcopy {
namespace crypto {

// ------------------------------------------------------------------
// Constants
// ------------------------------------------------------------------
static constexpr uint64_t PRIME64_1 = 0x9E3779B185EBCA87ULL;
static constexpr uint64_t PRIME64_2 = 0xC2B2AE3D27D4EB4FULL;
static constexpr uint64_t PRIME64_3 = 0x165667B19E3779F9ULL;
static constexpr uint64_t PRIME64_4 = 0x85EBCA77C2B2AE63ULL;
static constexpr uint64_t PRIME64_5 = 0x27D4EB2F165667C5ULL;

// ------------------------------------------------------------------
// Helpers
// ------------------------------------------------------------------
static inline uint64_t XXH_read64(const void* p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

static inline uint64_t XXH_round(uint64_t acc, uint64_t lane) {
    acc += lane * PRIME64_2;
    acc  = XXH_rotl64(acc, 31);
    acc *= PRIME64_1;
    return acc;
}

static inline uint64_t XXH_merge(uint64_t h, uint64_t v) {
    h ^= XXH_round(0, v);
    h  = h * PRIME64_1 + PRIME64_4;
    return h;
}

static inline uint64_t XXH_avalanche(uint64_t h) {
    h ^= h >> 33;
    h *= PRIME64_2;
    h ^= h >> 29;
    h *= PRIME64_3;
    h ^= h >> 32;
    return h;
}

// ------------------------------------------------------------------
// One-shot API
// ------------------------------------------------------------------
uint64_t xxhash64(const void* data, size_t len, uint64_t seed) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    const uint8_t* const end = p + len;
    uint64_t h;

    if (len >= 32) {
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + PRIME64_1 + PRIME64_2;
        uint64_t v2 = seed + PRIME64_2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - PRIME64_1;

        do {
            v1 = XXH_round(v1, XXH_read64(p));      p += 8;
            v2 = XXH_round(v2, XXH_read64(p));      p += 8;
            v3 = XXH_round(v3, XXH_read64(p));      p += 8;
            v4 = XXH_round(v4, XXH_read64(p));      p += 8;
        } while (p <= limit);

        h = XXH_rotl64(v1,  1) + XXH_rotl64(v2,  7) +
            XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);
        h = XXH_merge(h, v1);
        h = XXH_merge(h, v2);
        h = XXH_merge(h, v3);
        h = XXH_merge(h, v4);
    } else {
        h = seed + PRIME64_5;
    }

    h += static_cast<uint64_t>(len);

    // Process remaining 1..31 bytes
    while (p + 8 <= end) {
        uint64_t lane = XXH_read64(p);
        lane *= PRIME64_2;
        lane  = XXH_rotl64(lane, 31);
        lane *= PRIME64_1;
        h ^= lane;
        h  = XXH_rotl64(h, 27) * PRIME64_1 + PRIME64_4;
        p += 8;
    }
    if (p + 4 <= end) {
        uint32_t v32;
        std::memcpy(&v32, p, 4);
        h ^= static_cast<uint64_t>(v32) * PRIME64_1;
        h  = XXH_rotl64(h, 23) * PRIME64_2 + PRIME64_3;
        p += 4;
    }
    while (p < end) {
        h ^= static_cast<uint64_t>(*p) * PRIME64_5;
        h  = XXH_rotl64(h, 11) * PRIME64_1;
        p++;
    }

    return XXH_avalanche(h);
}

// ------------------------------------------------------------------
// Streaming hasher
// ------------------------------------------------------------------
XxHash64Hasher::XxHash64Hasher(uint64_t seed) : seed_(seed) {
    state_[0] = seed + PRIME64_1 + PRIME64_2;
    state_[1] = seed + PRIME64_2;
    state_[2] = seed;
    state_[3] = seed - PRIME64_1;
    total_len_ = 0;
    buffer_len_ = 0;
}

void XxHash64Hasher::update(const uint8_t* data, size_t len) {
    if (len == 0) return;
    total_len_ += len;
    const uint8_t* p = data;
    const uint8_t* const end = data + len;

    // If we have buffered data from a previous call, fill stripe first
    if (buffer_len_ > 0) {
        size_t to_copy = (len < (32 - buffer_len_)) ? len : (32 - buffer_len_);
        std::memcpy(buffer_ + buffer_len_, p, to_copy);
        buffer_len_ += to_copy;
        p += to_copy;

        if (buffer_len_ == 32) {
            state_[0] = XXH_round(state_[0], XXH_read64(buffer_));
            state_[1] = XXH_round(state_[1], XXH_read64(buffer_ + 8));
            state_[2] = XXH_round(state_[2], XXH_read64(buffer_ + 16));
            state_[3] = XXH_round(state_[3], XXH_read64(buffer_ + 24));
            buffer_len_ = 0;
        }
    }

    // Process full stripes directly from input
    while (p + 32 <= end) {
        state_[0] = XXH_round(state_[0], XXH_read64(p));
        state_[1] = XXH_round(state_[1], XXH_read64(p + 8));
        state_[2] = XXH_round(state_[2], XXH_read64(p + 16));
        state_[3] = XXH_round(state_[3], XXH_read64(p + 24));
        p += 32;
    }

    // Buffer remaining bytes
    if (p < end) {
        size_t rem = static_cast<size_t>(end - p);
        std::memcpy(buffer_, p, rem);
        buffer_len_ = rem;
    }
}

std::vector<uint8_t> XxHash64Hasher::finalize() {
    uint64_t h;

    if (total_len_ >= 32) {
        h = XXH_rotl64(state_[0],  1) + XXH_rotl64(state_[1],  7) +
            XXH_rotl64(state_[2], 12) + XXH_rotl64(state_[3], 18);
        h = XXH_merge(h, state_[0]);
        h = XXH_merge(h, state_[1]);
        h = XXH_merge(h, state_[2]);
        h = XXH_merge(h, state_[3]);
    } else {
        h = seed_ + PRIME64_5;
    }

    h += total_len_;

    // Process buffered stripe remainder
    const uint8_t* p = buffer_;
    const uint8_t* const end = buffer_ + buffer_len_;
    while (p + 8 <= end) {
        uint64_t lane = XXH_read64(p);
        lane *= PRIME64_2;
        lane  = XXH_rotl64(lane, 31);
        lane *= PRIME64_1;
        h ^= lane;
        h  = XXH_rotl64(h, 27) * PRIME64_1 + PRIME64_4;
        p += 8;
    }
    if (p + 4 <= end) {
        uint32_t v32;
        std::memcpy(&v32, p, 4);
        h ^= static_cast<uint64_t>(v32) * PRIME64_1;
        h  = XXH_rotl64(h, 23) * PRIME64_2 + PRIME64_3;
        p += 4;
    }
    while (p < end) {
        h ^= static_cast<uint64_t>(*p) * PRIME64_5;
        h  = XXH_rotl64(h, 11) * PRIME64_1;
        p++;
    }

    h = XXH_avalanche(h);

    std::vector<uint8_t> out(8);
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<uint8_t>(h >> (i * 8));
    return out;
}

} // namespace crypto
} // namespace netcopy
