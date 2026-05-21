// ML-KEM implementation using liboqs C API (FIPS 203)
#include "crypto/mlkem.h"
#include "exceptions.h"

#include <oqs/kem.h>
#include <stdexcept>
#include <cstring>

namespace netcopy {
namespace crypto {

// Map MlKemLevel to OQS algorithm name string
static const char* oqs_alg_name(MlKemLevel level) {
    switch (level) {
        case MlKemLevel::MLKEM_512:  return OQS_KEM_alg_ml_kem_512;
        case MlKemLevel::MLKEM_768:  return OQS_KEM_alg_ml_kem_768;
        case MlKemLevel::MLKEM_1024: return OQS_KEM_alg_ml_kem_1024;
    }
    return OQS_KEM_alg_ml_kem_768;
}

MlKemKeyPair MlKem::generate_keypair(MlKemLevel level) {
    OQS_KEM* kem = OQS_KEM_new(oqs_alg_name(level));
    if (!kem) {
        throw CryptoException("ML-KEM: OQS_KEM_new failed for level " + level_to_string(level));
    }

    MlKemKeyPair kp;
    kp.level = level;
    kp.public_key.resize(kem->length_public_key);
    kp.private_key.resize(kem->length_secret_key);

    OQS_STATUS rc = OQS_KEM_keypair(kem, kp.public_key.data(), kp.private_key.data());
    OQS_KEM_free(kem);

    if (rc != OQS_SUCCESS) {
        throw CryptoException("ML-KEM: OQS_KEM_keypair failed");
    }
    return kp;
}

MlKemEncapResult MlKem::encapsulate(const std::vector<uint8_t>& public_key, MlKemLevel level) {
    OQS_KEM* kem = OQS_KEM_new(oqs_alg_name(level));
    if (!kem) {
        throw CryptoException("ML-KEM: OQS_KEM_new failed for level " + level_to_string(level));
    }

    if (public_key.size() != kem->length_public_key) {
        OQS_KEM_free(kem);
        throw CryptoException("ML-KEM: public key size mismatch");
    }

    MlKemEncapResult result;
    result.ciphertext.resize(kem->length_ciphertext);
    result.shared_secret.resize(kem->length_shared_secret);

    OQS_STATUS rc = OQS_KEM_encaps(kem,
                                    result.ciphertext.data(),
                                    result.shared_secret.data(),
                                    public_key.data());
    OQS_KEM_free(kem);

    if (rc != OQS_SUCCESS) {
        throw CryptoException("ML-KEM: OQS_KEM_encaps failed");
    }
    return result;
}

std::vector<uint8_t> MlKem::decapsulate(const std::vector<uint8_t>& private_key,
                                          const std::vector<uint8_t>& ciphertext,
                                          MlKemLevel level) {
    OQS_KEM* kem = OQS_KEM_new(oqs_alg_name(level));
    if (!kem) {
        throw CryptoException("ML-KEM: OQS_KEM_new failed for level " + level_to_string(level));
    }

    if (private_key.size() != kem->length_secret_key) {
        OQS_KEM_free(kem);
        throw CryptoException("ML-KEM: private key size mismatch");
    }
    if (ciphertext.size() != kem->length_ciphertext) {
        OQS_KEM_free(kem);
        throw CryptoException("ML-KEM: ciphertext size mismatch");
    }

    std::vector<uint8_t> shared_secret(kem->length_shared_secret);

    OQS_STATUS rc = OQS_KEM_decaps(kem,
                                    shared_secret.data(),
                                    ciphertext.data(),
                                    private_key.data());
    OQS_KEM_free(kem);

    if (rc != OQS_SUCCESS) {
        throw CryptoException("ML-KEM: OQS_KEM_decaps failed");
    }
    return shared_secret;
}

std::string MlKem::level_to_string(MlKemLevel level) {
    switch (level) {
        case MlKemLevel::MLKEM_512:  return "ML-KEM-512";
        case MlKemLevel::MLKEM_768:  return "ML-KEM-768";
        case MlKemLevel::MLKEM_1024: return "ML-KEM-1024";
    }
    return "ML-KEM-768";
}

MlKemLevel MlKem::string_to_level(const std::string& s) {
    if (s == "ML-KEM-512"  || s == "512")  return MlKemLevel::MLKEM_512;
    if (s == "ML-KEM-768"  || s == "768")  return MlKemLevel::MLKEM_768;
    if (s == "ML-KEM-1024" || s == "1024") return MlKemLevel::MLKEM_1024;
    throw CryptoException("Unknown ML-KEM level: " + s);
}

size_t MlKem::public_key_size(MlKemLevel level) {
    switch (level) {
        case MlKemLevel::MLKEM_512:  return 800;
        case MlKemLevel::MLKEM_768:  return 1184;
        case MlKemLevel::MLKEM_1024: return 1568;
    }
    return 1184;
}

size_t MlKem::private_key_size(MlKemLevel level) {
    switch (level) {
        case MlKemLevel::MLKEM_512:  return 1632;
        case MlKemLevel::MLKEM_768:  return 2400;
        case MlKemLevel::MLKEM_1024: return 3168;
    }
    return 2400;
}

size_t MlKem::ciphertext_size(MlKemLevel level) {
    switch (level) {
        case MlKemLevel::MLKEM_512:  return 768;
        case MlKemLevel::MLKEM_768:  return 1088;
        case MlKemLevel::MLKEM_1024: return 1568;
    }
    return 1088;
}

} // namespace crypto
} // namespace netcopy
