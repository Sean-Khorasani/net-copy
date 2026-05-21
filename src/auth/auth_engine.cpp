// Authentication engine: challenge/response for password and ML-KEM auth.
// Password protocol:
//   Server sends: challenge_nonce (32 bytes), salt_hex, pbkdf2_iterations
//   Client proof: SHA3-256(PBKDF2(password, salt, iters, 32) || challenge_nonce)
//   Server verifies: SHA3-256(stored_hash_bytes || challenge_nonce) == client_proof
//
// ML-KEM protocol:
//   Server encapsulates with user's public key, sends ciphertext + kem_nonce
//   Client decapsulates, computes SHA3-256(shared_secret || kem_nonce)
//   Server verifies: SHA3-256(shared_secret_from_encaps || kem_nonce) == client_proof
#include "auth/auth_engine.h"
#include "crypto/sha3.h"
#include "crypto/mlkem.h"
#include "exceptions.h"

namespace netcopy {
namespace auth {

AuthEngine::AuthEngine(const UserDb& db) : db_(db) {}

AuthChallengeData AuthEngine::prepare_challenge(const std::string& username, AuthMethod method) {
    const auto* user = db_.find_user(username);
    if (!user) {
        throw AuthException("User not found: " + username);
    }

    AuthChallengeData ch;
    ch.method = method;

    if (method == AuthMethod::PASSWORD) {
        if (!user->has_auth_method("password")) {
            throw AuthException("Password auth not enabled for user: " + username);
        }
        ch.challenge_nonce   = crypto::random_bytes(32);
        ch.salt_hex          = user->salt_hex;
        ch.pbkdf2_iterations = user->pbkdf2_iterations;

        // expected_proof = SHA3-256(stored_pbkdf2_hash_bytes || challenge_nonce)
        auto stored_hash = crypto::hex_to_bytes(user->password_hash_hex);
        std::vector<uint8_t> preimage;
        preimage.reserve(stored_hash.size() + ch.challenge_nonce.size());
        preimage.insert(preimage.end(), stored_hash.begin(), stored_hash.end());
        preimage.insert(preimage.end(), ch.challenge_nonce.begin(), ch.challenge_nonce.end());
        ch.expected_proof = crypto::sha3_256(preimage);

    } else if (method == AuthMethod::MLKEM) {
        if (!user->has_auth_method("mlkem")) {
            throw AuthException("ML-KEM auth not enabled for user: " + username);
        }
        if (user->mlkem_public_key.empty()) {
            throw AuthException("No ML-KEM public key registered for user: " + username);
        }

        ch.mlkem_level = user->mlkem_level;
        auto encap_result = crypto::MlKem::encapsulate(user->mlkem_public_key, user->mlkem_level);
        ch.kem_ciphertext = encap_result.ciphertext;
        ch.kem_nonce      = crypto::random_bytes(32);

        // expected_proof = SHA3-256(shared_secret || kem_nonce)
        std::vector<uint8_t> preimage;
        preimage.reserve(encap_result.shared_secret.size() + ch.kem_nonce.size());
        preimage.insert(preimage.end(), encap_result.shared_secret.begin(), encap_result.shared_secret.end());
        preimage.insert(preimage.end(), ch.kem_nonce.begin(), ch.kem_nonce.end());
        ch.expected_proof = crypto::sha3_256(preimage);

    } else {
        throw AuthException("Unsupported auth method");
    }

    return ch;
}

bool AuthEngine::verify_response(const AuthChallengeData& challenge,
                                  const std::vector<uint8_t>& client_proof) {
    if (client_proof.size() != challenge.expected_proof.size()) return false;

    // Constant-time comparison
    uint8_t diff = 0;
    for (size_t i = 0; i < client_proof.size(); ++i) {
        diff |= client_proof[i] ^ challenge.expected_proof[i];
    }
    return diff == 0;
}

} // namespace auth
} // namespace netcopy
