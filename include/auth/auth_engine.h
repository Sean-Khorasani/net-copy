#pragma once
#include "auth/user_db.h"
#include <cstdint>
#include <string>
#include <vector>

namespace netcopy {
namespace auth {

enum class AuthMethod { NONE = 0, PASSWORD = 1, MLKEM = 2 };

struct AuthChallengeData {
    AuthMethod method;
    // For password auth:
    std::vector<uint8_t> challenge_nonce; // random 32 bytes
    std::string salt_hex;                 // user's stored salt
    int pbkdf2_iterations = 0;
    // For ML-KEM auth:
    std::vector<uint8_t> kem_ciphertext;  // from Encaps(user_pubkey)
    std::vector<uint8_t> kem_nonce;       // random 32 bytes mixed into proof
    crypto::MlKemLevel mlkem_level = crypto::MlKemLevel::MLKEM_768;
    // Internal (not sent to client):
    std::vector<uint8_t> expected_proof;  // pre-computed for verification
    std::vector<uint8_t> kem_shared_secret; // ML-KEM shared secret for key derivation
};

class AuthEngine {
public:
    explicit AuthEngine(const UserDb& db);

    // Returns challenge data to send to client.
    // Throws AuthException if user not found or method not allowed.
    AuthChallengeData prepare_challenge(const std::string& username, AuthMethod method);

    // Verifies the client's proof against the challenge.
    bool verify_response(const AuthChallengeData& challenge,
                         const std::vector<uint8_t>& client_proof);

    // Direct password verification (e.g. for SSH/SFTP passwords)
    bool verify_password(const std::string& username, const std::string& password) const;

private:
    const UserDb& db_;
};

} // namespace auth
} // namespace netcopy
