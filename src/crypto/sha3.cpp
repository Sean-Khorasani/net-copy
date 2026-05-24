// SHA3-256 (FIPS 202 Keccak-f[1600]), HMAC-SHA3-256, PBKDF2-HMAC-SHA3-256
// Base64 encode/decode, hex encode/decode, cryptographically secure random bytes
#include "crypto/sha3.h"
#include "exceptions.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#ifdef _WIN32
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#else
#  include <fstream>
#endif

namespace netcopy {
namespace crypto {

// ============================================================
// Internal Keccak-f[1600] implementation
// ============================================================
namespace {

// Round constants for 24 rounds of Keccak-f[1600]
static const uint64_t KECCAK_ROUND_CONSTANTS[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808AULL, 0x8000000080008000ULL,
    0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

// Rho rotation offsets (pi-permuted order for lanes)
static const int KECCAK_RHO[24] = {
     1,  3,  6, 10, 15, 21, 28, 36, 45, 55,  2, 14,
    27, 41, 56,  8, 25, 43, 62, 18, 39, 61, 20, 44
};

// Pi permutation: lane (x,y) -> (y, 2x+3y) mod 5
// These are the source indices in the 5x5 grid (linearized as y*5+x)
static const int KECCAK_PI[24] = {
    10,  7, 11, 17, 18,  3,  5, 16,  8, 21, 24,  4,
    15, 23, 19, 13, 12,  2, 20, 14, 22,  9,  6,  1
};

static inline uint64_t rotl64(uint64_t x, int n) {
    return (x << n) | (x >> (64 - n));
}

// Full Keccak-f[1600] permutation on 25-lane (5x5) state
static void keccak_f1600(uint64_t state[25]) {
    for (int round = 0; round < 24; ++round) {
        // Theta step
        uint64_t C[5];
        for (int x = 0; x < 5; ++x) {
            C[x] = state[x] ^ state[x+5] ^ state[x+10] ^ state[x+15] ^ state[x+20];
        }
        uint64_t D[5];
        for (int x = 0; x < 5; ++x) {
            D[x] = C[(x+4) % 5] ^ rotl64(C[(x+1) % 5], 1);
        }
        for (int i = 0; i < 25; ++i) {
            state[i] ^= D[i % 5];
        }

        // Rho and Pi steps (combined rotation + permutation)
        // Standard Keccak approach: iterate 24 lanes in PI order, rotating each
        {
            uint64_t last = state[1];
            for (int i = 0; i < 24; ++i) {
                int j = KECCAK_PI[i];
                uint64_t temp = state[j];
                state[j] = rotl64(last, KECCAK_RHO[i]);
                last = temp;
            }
        }

        // Chi step
        for (int y = 0; y < 5; ++y) {
            uint64_t row[5];
            for (int x = 0; x < 5; ++x) row[x] = state[y*5+x];
            for (int x = 0; x < 5; ++x) {
                state[y*5+x] = row[x] ^ ((~row[(x+1)%5]) & row[(x+2)%5]);
            }
        }

        // Iota step
        state[0] ^= KECCAK_ROUND_CONSTANTS[round];
    }
}

// SHA3-256 parameters
static constexpr size_t SHA3_256_RATE    = 136; // 1088 bits / 8
static constexpr size_t SHA3_256_CAPACITY = 64;  // 512 bits / 8 (not directly used)
static constexpr size_t SHA3_256_OUTPUT   = 32;  // 256 bits / 8

// Absorb data into sponge state and squeeze 32 bytes output
static std::vector<uint8_t> keccak_sha3_256(const uint8_t* data, size_t len) {
    uint64_t state[25] = {};

    const size_t RATE = SHA3_256_RATE;

    // Absorb full blocks
    size_t offset = 0;
    while (offset + RATE <= len) {
        for (size_t i = 0; i < RATE / 8; ++i) {
            uint64_t lane = 0;
            for (int b = 0; b < 8; ++b) {
                lane |= static_cast<uint64_t>(data[offset + i*8 + b]) << (b * 8);
            }
            state[i] ^= lane;
        }
        keccak_f1600(state);
        offset += RATE;
    }

    // Pad remaining data (SHA3 domain separator 0x06)
    uint8_t last_block[SHA3_256_RATE] = {};
    size_t remaining = len - offset;
    std::memcpy(last_block, data + offset, remaining);
    last_block[remaining] = 0x06;          // SHA3 padding
    last_block[RATE - 1] |= 0x80;          // trailing bit

    // Absorb last block
    for (size_t i = 0; i < RATE / 8; ++i) {
        uint64_t lane = 0;
        for (int b = 0; b < 8; ++b) {
            lane |= static_cast<uint64_t>(last_block[i*8 + b]) << (b * 8);
        }
        state[i] ^= lane;
    }
    keccak_f1600(state);

    // Squeeze 32 bytes
    std::vector<uint8_t> digest(SHA3_256_OUTPUT);
    for (size_t i = 0; i < SHA3_256_OUTPUT / 8; ++i) {
        for (int b = 0; b < 8; ++b) {
            digest[i*8 + b] = static_cast<uint8_t>((state[i] >> (b * 8)) & 0xFF);
        }
    }
    return digest;
}

// Base64 alphabet
static const char B64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // anonymous namespace

// ============================================================
// Public SHA3-256 API
// ============================================================

std::vector<uint8_t> sha3_256(const uint8_t* data, size_t len) {
    return keccak_sha3_256(data, len);
}

std::vector<uint8_t> sha3_256(const std::vector<uint8_t>& data) {
    return keccak_sha3_256(data.data(), data.size());
}

std::vector<uint8_t> sha3_256(const std::string& data) {
    return keccak_sha3_256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

// ============================================================
// HMAC-SHA3-256
// ============================================================

std::vector<uint8_t> hmac_sha3_256(const std::vector<uint8_t>& key,
                                    const std::vector<uint8_t>& data) {
    const size_t BLOCK_SIZE = SHA3_256_RATE; // 136 bytes for SHA3-256

    // If key is longer than block size, hash it
    std::vector<uint8_t> k = key;
    if (k.size() > BLOCK_SIZE) {
        k = sha3_256(k);
    }
    k.resize(BLOCK_SIZE, 0);

    // i_key_pad = k XOR 0x36..., o_key_pad = k XOR 0x5C...
    std::vector<uint8_t> i_key_pad(BLOCK_SIZE);
    std::vector<uint8_t> o_key_pad(BLOCK_SIZE);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        i_key_pad[i] = k[i] ^ 0x36;
        o_key_pad[i] = k[i] ^ 0x5C;
    }

    // inner = SHA3-256(i_key_pad || data)
    std::vector<uint8_t> inner_preimage;
    inner_preimage.reserve(BLOCK_SIZE + data.size());
    inner_preimage.insert(inner_preimage.end(), i_key_pad.begin(), i_key_pad.end());
    inner_preimage.insert(inner_preimage.end(), data.begin(), data.end());
    auto inner = sha3_256(inner_preimage);

    // outer = SHA3-256(o_key_pad || inner)
    std::vector<uint8_t> outer_preimage;
    outer_preimage.reserve(BLOCK_SIZE + inner.size());
    outer_preimage.insert(outer_preimage.end(), o_key_pad.begin(), o_key_pad.end());
    outer_preimage.insert(outer_preimage.end(), inner.begin(), inner.end());
    return sha3_256(outer_preimage);
}

// ============================================================
// PBKDF2-HMAC-SHA3-256
// ============================================================

std::vector<uint8_t> pbkdf2_sha3_256(const std::string& password,
                                      const std::vector<uint8_t>& salt,
                                      int iterations,
                                      size_t output_len) {
    const size_t HASH_LEN = 32; // SHA3-256 output
    std::vector<uint8_t> key_bytes(reinterpret_cast<const uint8_t*>(password.data()),
                                   reinterpret_cast<const uint8_t*>(password.data()) + password.size());

    std::vector<uint8_t> result;
    result.reserve(output_len);

    uint32_t block_count = static_cast<uint32_t>((output_len + HASH_LEN - 1) / HASH_LEN);

    for (uint32_t block_index = 1; block_index <= block_count; ++block_index) {
        // U1 = HMAC(password, salt || INT(block_index))  -- block_index big-endian
        std::vector<uint8_t> prfinput;
        prfinput.reserve(salt.size() + 4);
        prfinput.insert(prfinput.end(), salt.begin(), salt.end());
        prfinput.push_back(static_cast<uint8_t>((block_index >> 24) & 0xFF));
        prfinput.push_back(static_cast<uint8_t>((block_index >> 16) & 0xFF));
        prfinput.push_back(static_cast<uint8_t>((block_index >>  8) & 0xFF));
        prfinput.push_back(static_cast<uint8_t>( block_index        & 0xFF));

        auto U = hmac_sha3_256(key_bytes, prfinput);
        auto T = U;

        for (int iter = 1; iter < iterations; ++iter) {
            U = hmac_sha3_256(key_bytes, U);
            for (size_t i = 0; i < HASH_LEN; ++i) {
                T[i] ^= U[i];
            }
        }

        result.insert(result.end(), T.begin(), T.end());
    }

    result.resize(output_len);
    return result;
}

// ============================================================
// Hex encode/decode
// ============================================================

std::string bytes_to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : data) {
        oss << std::setw(2) << static_cast<unsigned>(b);
    }
    return oss.str();
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw CryptoException("hex_to_bytes: odd-length hex string");
    }
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned byte_val;
        std::istringstream ss(hex.substr(i, 2));
        ss >> std::hex >> byte_val;
        result.push_back(static_cast<uint8_t>(byte_val));
    }
    return result;
}

// ============================================================
// Base64 encode/decode (RFC 4648)
// ============================================================

std::string base64_encode(const std::vector<uint8_t>& data) {
    std::string result;
    size_t len = data.size();
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint8_t b0 = data[i];
        uint8_t b1 = (i + 1 < len) ? data[i+1] : 0;
        uint8_t b2 = (i + 2 < len) ? data[i+2] : 0;

        result += B64_CHARS[(b0 >> 2) & 0x3F];
        result += B64_CHARS[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
        result += (i + 1 < len) ? B64_CHARS[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=';
        result += (i + 2 < len) ? B64_CHARS[b2 & 0x3F] : '=';
    }
    return result;
}

std::vector<uint8_t> base64_decode(const std::string& b64) {
    static const int DECODE_TABLE[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::vector<uint8_t> result;
    int val = 0, valb = -8;
    for (unsigned char c : b64) {
        if (c == '=') break;
        if (DECODE_TABLE[c] == -1) continue;
        val = (val << 6) + DECODE_TABLE[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

// ============================================================
// Cryptographically secure random bytes
// ============================================================

std::vector<uint8_t> random_bytes(size_t count) {
    std::vector<uint8_t> buf(count);
    if (count == 0) return buf;

#ifdef _WIN32
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        buf.data(),
        static_cast<ULONG>(count),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        throw CryptoException("BCryptGenRandom failed with status " + std::to_string(status));
    }
#else
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom) {
        throw CryptoException("Failed to open /dev/urandom");
    }
    urandom.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(count));
    if (!urandom) {
        throw CryptoException("Failed to read from /dev/urandom");
    }
#endif
    return buf;
}

Sha3Hasher::Sha3Hasher() : buffer_len_(0) {
    std::memset(state_, 0, sizeof(state_));
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha3Hasher::update(const uint8_t* data, size_t len) {
    size_t offset = 0;
    
    if (buffer_len_ > 0) {
        size_t to_copy = (std::min)(len, size_t(136) - buffer_len_);
        std::memcpy(buffer_ + buffer_len_, data, to_copy);
        buffer_len_ += to_copy;
        offset += to_copy;
        
        if (buffer_len_ == 136) {
            for (size_t i = 0; i < 17; ++i) {
                uint64_t lane = 0;
                for (int b = 0; b < 8; ++b) {
                    lane |= static_cast<uint64_t>(buffer_[i*8 + b]) << (b * 8);
                }
                state_[i] ^= lane;
            }
            keccak_f1600(state_);
            buffer_len_ = 0;
        }
    }
    
    while (offset + 136 <= len) {
        for (size_t i = 0; i < 17; ++i) {
            uint64_t lane = 0;
            for (int b = 0; b < 8; ++b) {
                lane |= static_cast<uint64_t>(data[offset + i*8 + b]) << (b * 8);
            }
            state_[i] ^= lane;
        }
        keccak_f1600(state_);
        offset += 136;
    }
    
    if (offset < len) {
        size_t remaining = len - offset;
        std::memcpy(buffer_ + buffer_len_, data + offset, remaining);
        buffer_len_ += remaining;
    }
}

std::vector<uint8_t> Sha3Hasher::finalize() {
    uint8_t last_block[136] = {};
    std::memcpy(last_block, buffer_, buffer_len_);
    last_block[buffer_len_] = 0x06;
    last_block[135] |= 0x80;
    
    for (size_t i = 0; i < 17; ++i) {
        uint64_t lane = 0;
        for (int b = 0; b < 8; ++b) {
            lane |= static_cast<uint64_t>(last_block[i*8 + b]) << (b * 8);
        }
        state_[i] ^= lane;
    }
    keccak_f1600(state_);
    
    std::vector<uint8_t> digest(32);
    for (size_t i = 0; i < 4; ++i) {
        for (int b = 0; b < 8; ++b) {
            digest[i*8 + b] = static_cast<uint8_t>((state_[i] >> (b * 8)) & 0xFF);
        }
    }
    return digest;
}

} // namespace crypto
} // namespace netcopy
