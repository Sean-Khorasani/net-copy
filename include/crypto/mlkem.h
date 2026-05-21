#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace netcopy {
namespace crypto {

enum class MlKemLevel { MLKEM_512, MLKEM_768, MLKEM_1024 };

struct MlKemKeyPair {
    std::vector<uint8_t> public_key;
    std::vector<uint8_t> private_key;
    MlKemLevel level;
};

struct MlKemEncapResult {
    std::vector<uint8_t> ciphertext;
    std::vector<uint8_t> shared_secret; // always 32 bytes
};

class MlKem {
public:
    static MlKemKeyPair generate_keypair(MlKemLevel level);
    static MlKemEncapResult encapsulate(const std::vector<uint8_t>& public_key, MlKemLevel level);
    static std::vector<uint8_t> decapsulate(const std::vector<uint8_t>& private_key,
                                             const std::vector<uint8_t>& ciphertext,
                                             MlKemLevel level);
    static std::string level_to_string(MlKemLevel level); // "ML-KEM-512", "ML-KEM-768", "ML-KEM-1024"
    static MlKemLevel string_to_level(const std::string& s);
    static size_t public_key_size(MlKemLevel level);
    static size_t private_key_size(MlKemLevel level);
    static size_t ciphertext_size(MlKemLevel level);
};

} // namespace crypto
} // namespace netcopy
