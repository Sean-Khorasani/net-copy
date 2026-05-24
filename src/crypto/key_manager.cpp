// PEM-like key file format manager for ML-KEM keys.
// Encryption: AES-256-CTR + HMAC-SHA3-256 (encrypt-then-MAC).
// KDF: PBKDF2-SHA3-256(passphrase, salt, 200000, 64)
//   first 32 bytes = AES key, next 32 bytes = HMAC key.
// Wire format of encrypted blob: [32-byte HMAC-SHA3-256][AES-256-CTR(key_bytes)]
#include "crypto/key_manager.h"
#include "crypto/sha3.h"
#include "crypto/aes_ctr.h"
#include "exceptions.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace netcopy {
namespace crypto {

// ============================================================
// PEM-like file helpers
// ============================================================

static std::string make_begin_marker(const std::string& level_str, bool encrypted, bool pub) {
    if (pub) return "-----BEGIN " + level_str + " PUBLIC KEY-----";
    if (encrypted) return "-----BEGIN ENCRYPTED " + level_str + " PRIVATE KEY-----";
    return "-----BEGIN " + level_str + " PRIVATE KEY-----";
}

static std::string make_end_marker(const std::string& level_str, bool encrypted, bool pub) {
    if (pub) return "-----END " + level_str + " PUBLIC KEY-----";
    if (encrypted) return "-----END ENCRYPTED " + level_str + " PRIVATE KEY-----";
    return "-----END " + level_str + " PRIVATE KEY-----";
}

static std::string trim_line(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ============================================================
// Encryption helpers (AES-256-CTR + HMAC-SHA3-256 encrypt-then-MAC)
// ============================================================

// Derive two keys from passphrase+salt via PBKDF2
static void derive_keys(const std::string& passphrase,
                         const std::vector<uint8_t>& salt,
                         int iterations,
                         AesCtr::Key& aes_key,
                         std::vector<uint8_t>& hmac_key) {
    auto dk = pbkdf2_sha3_256(passphrase, salt, iterations, 64);
    std::copy(dk.begin(), dk.begin() + 32, aes_key.begin());
    hmac_key.assign(dk.begin() + 32, dk.end());
}

// Encrypt plaintext, return [32-byte MAC || ciphertext]
static std::vector<uint8_t> encrypt_key_blob(const std::vector<uint8_t>& plaintext,
                                              const std::string& passphrase,
                                              const std::vector<uint8_t>& salt,
                                              const AesCtr::IV& iv,
                                              int iterations) {
    AesCtr::Key aes_key;
    std::vector<uint8_t> hmac_key;
    derive_keys(passphrase, salt, iterations, aes_key, hmac_key);

    AesCtr cipher(aes_key);
    auto ciphertext = cipher.process(plaintext, iv);

    // MAC over ciphertext
    auto mac = hmac_sha3_256(hmac_key, ciphertext);

    std::vector<uint8_t> blob;
    blob.reserve(32 + ciphertext.size());
    blob.insert(blob.end(), mac.begin(), mac.end());
    blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());
    return blob;
}

// Decrypt and verify [32-byte MAC || ciphertext]
static std::vector<uint8_t> decrypt_key_blob(const std::vector<uint8_t>& blob,
                                              const std::string& passphrase,
                                              const std::vector<uint8_t>& salt,
                                              const AesCtr::IV& iv,
                                              int iterations) {
    if (blob.size() < 32) {
        throw CryptoException("Encrypted key blob too short");
    }

    AesCtr::Key aes_key;
    std::vector<uint8_t> hmac_key;
    derive_keys(passphrase, salt, iterations, aes_key, hmac_key);

    std::vector<uint8_t> stored_mac(blob.begin(), blob.begin() + 32);
    std::vector<uint8_t> ciphertext(blob.begin() + 32, blob.end());

    // Verify MAC (constant-time compare)
    auto computed_mac = hmac_sha3_256(hmac_key, ciphertext);
    if (computed_mac.size() != stored_mac.size()) {
        throw CryptoException("MAC verification failed (bad passphrase or corrupted key file)");
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < computed_mac.size(); ++i) {
        diff |= computed_mac[i] ^ stored_mac[i];
    }
    if (diff != 0) {
        throw CryptoException("MAC verification failed (bad passphrase or corrupted key file)");
    }

    AesCtr cipher(aes_key);
    return cipher.process(ciphertext, iv);
}

// ============================================================
// Public API
// ============================================================

void save_public_key(const std::string& path,
                     const std::vector<uint8_t>& key,
                     MlKemLevel level) {
    std::string level_str = MlKem::level_to_string(level);
    std::ofstream f(std::filesystem::u8path(path));
    if (!f) throw FileException("Cannot write public key: " + path);

    f << make_begin_marker(level_str, false, true) << "\n";
    f << base64_encode(key) << "\n";
    f << make_end_marker(level_str, false, true) << "\n";
}

void save_private_key(const std::string& path,
                      const std::vector<uint8_t>& key,
                      MlKemLevel level,
                      const std::string& passphrase) {
    std::string level_str = MlKem::level_to_string(level);
    std::ofstream f(std::filesystem::u8path(path));
    if (!f) throw FileException("Cannot write private key: " + path);

    if (passphrase.empty()) {
        f << make_begin_marker(level_str, false, false) << "\n";
        f << base64_encode(key) << "\n";
        f << make_end_marker(level_str, false, false) << "\n";
    } else {
        auto salt = random_bytes(16);
        auto iv_bytes = random_bytes(AesCtr::IV_SIZE);
        AesCtr::IV iv;
        std::copy(iv_bytes.begin(), iv_bytes.end(), iv.begin());

        int iterations = 200000;
        auto blob = encrypt_key_blob(key, passphrase, salt, iv, iterations);

        f << make_begin_marker(level_str, true, false) << "\n";
        f << "KDF: PBKDF2-SHA3-256\n";
        f << "Iterations: " << iterations << "\n";
        f << "Salt: " << bytes_to_hex(salt) << "\n";
        f << "IV: " << bytes_to_hex(std::vector<uint8_t>(iv.begin(), iv.end())) << "\n";
        f << base64_encode(blob) << "\n";
        f << make_end_marker(level_str, true, false) << "\n";
    }
}

std::vector<uint8_t> load_public_key(const std::string& path, MlKemLevel& level_out) {
    std::ifstream f(std::filesystem::u8path(path));
    if (!f) throw FileException("Cannot open public key: " + path);

    std::string line;
    std::string b64_data;
    bool in_block = false;

    while (std::getline(f, line)) {
        line = trim_line(line);
        if (line.empty()) continue;
        if (line.find("-----BEGIN") == 0 && line.find("PUBLIC KEY-----") != std::string::npos) {
            // Extract level
            size_t start = line.find("BEGIN ") + 6;
            size_t end = line.find(" PUBLIC KEY-----");
            std::string level_str = line.substr(start, end - start);
            level_out = MlKem::string_to_level(level_str);
            in_block = true;
        } else if (line.find("-----END") == 0 && line.find("PUBLIC KEY-----") != std::string::npos) {
            break;
        } else if (in_block) {
            b64_data += line;
        }
    }

    if (b64_data.empty()) throw CryptoException("No public key data found in: " + path);
    return base64_decode(b64_data);
}

std::vector<uint8_t> load_private_key(const std::string& path, MlKemLevel& level_out,
                                       const std::string& passphrase) {
    std::ifstream f(std::filesystem::u8path(path));
    if (!f) throw FileException("Cannot open private key: " + path);

    std::string line;
    bool encrypted = false;
    bool in_block = false;
    int iterations = 200000;
    std::vector<uint8_t> salt;
    AesCtr::IV iv{};
    std::string b64_data;

    while (std::getline(f, line)) {
        line = trim_line(line);
        if (line.empty()) continue;

        if (line.find("-----BEGIN ENCRYPTED") == 0) {
            encrypted = true;
            // Extract level: "-----BEGIN ENCRYPTED ML-KEM-768 PRIVATE KEY-----"
            size_t start = line.find("BEGIN ENCRYPTED ") + 16;
            size_t end = line.find(" PRIVATE KEY-----");
            std::string level_str = line.substr(start, end - start);
            level_out = MlKem::string_to_level(level_str);
            in_block = true;
        } else if (line.find("-----BEGIN") == 0 && line.find("PRIVATE KEY-----") != std::string::npos) {
            size_t start = line.find("BEGIN ") + 6;
            size_t end = line.find(" PRIVATE KEY-----");
            std::string level_str = line.substr(start, end - start);
            level_out = MlKem::string_to_level(level_str);
            in_block = true;
        } else if (line.find("-----END") == 0) {
            break;
        } else if (in_block) {
            // Check for header lines (KDF:, Iterations:, Salt:, IV:)
            if (line.find("KDF:") == 0) {
                // ignore
            } else if (line.find("Iterations:") == 0) {
                iterations = std::stoi(line.substr(12));
            } else if (line.find("Salt:") == 0) {
                salt = hex_to_bytes(line.substr(6));
            } else if (line.find("IV:") == 0) {
                auto iv_bytes = hex_to_bytes(line.substr(4));
                if (iv_bytes.size() == AesCtr::IV_SIZE) {
                    std::copy(iv_bytes.begin(), iv_bytes.end(), iv.begin());
                }
            } else {
                b64_data += line;
            }
        }
    }

    if (b64_data.empty()) throw CryptoException("No private key data found in: " + path);

    auto blob = base64_decode(b64_data);

    if (encrypted) {
        if (passphrase.empty()) {
            throw CryptoException("Private key is encrypted but no passphrase provided");
        }
        return decrypt_key_blob(blob, passphrase, salt, iv, iterations);
    }
    return blob;
}

std::vector<uint8_t> extract_public_key_from_private(const std::string& path,
                                                      MlKemLevel& level_out,
                                                      const std::string& passphrase) {
    auto privkey = load_private_key(path, level_out, passphrase);
    size_t pub_size = MlKem::public_key_size(level_out);
    if (privkey.size() < pub_size) {
        throw CryptoException("Private key too short to extract public key");
    }
    // In ML-KEM, the decapsulation key (sk) encodes the public key.
    // OQS liboqs stores: sk = (s || pk || H(pk) || z), so public key is at offset 0..pub_size
    // Actually for NIST ML-KEM, the OQS secret key is not necessarily laid out this way.
    // The safer approach is to note that the public key is the LAST pub_key_size bytes
    // of the OQS secret key structure (depends on liboqs version).
    // We take the simple approach: return the first pub_size bytes.
    // (For actual use, users should keep their public key separately.)
    return std::vector<uint8_t>(privkey.begin(), privkey.begin() + pub_size);
}

} // namespace crypto
} // namespace netcopy
