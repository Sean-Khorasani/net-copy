#pragma once
#include "crypto/mlkem.h"
#include <string>
#include <vector>
#include <memory>

namespace netcopy {
namespace auth {

struct UserEntry {
    std::string username;
    std::vector<std::string> auth_methods; // "password", "mlkem"
    // Password auth fields
    std::string password_hash_hex;  // PBKDF2-SHA3-256 hex output (32 bytes = 64 hex chars)
    std::string salt_hex;           // random 16-byte salt as hex
    int pbkdf2_iterations = 200000;
    // ML-KEM auth fields
    crypto::MlKemLevel mlkem_level = crypto::MlKemLevel::MLKEM_768;
    std::vector<uint8_t> mlkem_public_key; // decoded public key bytes
    // Authorization
    std::vector<std::string> allowed_paths; // {"*"} means all paths allowed

    bool has_auth_method(const std::string& method) const;
    bool can_access_path(const std::string& path) const;
};

class UserDb {
public:
    UserDb() = default;
    static UserDb load(const std::string& path);
    void save(const std::string& path) const;
    void save() const; // save to original path

    bool user_exists(const std::string& username) const;
    const UserEntry* find_user(const std::string& username) const;
    UserEntry* find_user_mutable(const std::string& username);

    void add_user(const UserEntry& entry);
    void update_password(const std::string& username, const std::string& new_password);
    void update_public_key(const std::string& username,
                           const std::vector<uint8_t>& pubkey,
                           crypto::MlKemLevel level);
    void remove_user(const std::string& username);
    const std::vector<UserEntry>& users() const { return users_; }

    // Auth verification
    bool verify_password(const std::string& username, const std::string& password) const;

    bool is_loaded() const { return loaded_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    std::vector<UserEntry> users_;
    bool loaded_ = false;

    static std::string escape_field(const std::string& s);
    static std::string unescape_field(const std::string& s);
};

} // namespace auth
} // namespace netcopy
