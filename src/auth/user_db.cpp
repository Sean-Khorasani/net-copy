// User database with CSV-based storage (semicolon-separated).
// Format: username;auth_methods;password_hash_hex;salt_hex;iterations;mlkem_level;mlkem_pubkey_b64;allowed_paths
#include "auth/user_db.h"
#include "crypto/sha3.h"
#include "exceptions.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace netcopy {
namespace auth {

// ============================================================
// Utility helpers
// ============================================================

static std::vector<std::string> split_by(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Simple escaping: replace ';' with '\;' and '\' with '\\'
// static
std::string UserDb::escape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == ';') out += "\\;";
        else out += c;
    }
    return out;
}

// static
std::string UserDb::unescape_field(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool esc = false;
    for (char c : s) {
        if (esc) { out += c; esc = false; }
        else if (c == '\\') esc = true;
        else out += c;
    }
    return out;
}

static std::vector<std::string> split_semicolon_raw(const std::string& line) {
    // Split by ';', but respect '\;' escapes
    std::vector<std::string> fields;
    std::string cur;
    bool esc = false;
    for (char c : line) {
        if (esc) {
            cur += '\\';
            cur += c;
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == ';') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

// ============================================================
// UserEntry methods
// ============================================================

bool UserEntry::has_auth_method(const std::string& method) const {
    return std::find(auth_methods.begin(), auth_methods.end(), method) != auth_methods.end();
}

bool UserEntry::can_access_path(const std::string& path) const {
    for (const auto& ap : allowed_paths) {
        if (ap == "*") return true;
        // Check if ap is a prefix of path
        if (path.size() >= ap.size() &&
            path.compare(0, ap.size(), ap) == 0) {
            // Make sure it's a real prefix (exact match or followed by separator)
            if (path.size() == ap.size() ||
                path[ap.size()] == '/' ||
                path[ap.size()] == '\\') {
                return true;
            }
        }
    }
    return false;
}

// ============================================================
// UserDb methods
// ============================================================

// static
UserDb UserDb::load(const std::string& path) {
    UserDb db;
    db.path_ = path;

    std::ifstream f(path);
    if (!f) {
        // File doesn't exist yet - return empty db
        db.loaded_ = false;
        return db;
    }
    db.loaded_ = true;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto fields = split_semicolon_raw(line);
        // Ensure at least 8 fields
        while (fields.size() < 8) fields.push_back("");

        UserEntry entry;
        entry.username          = UserDb::unescape_field(fields[0]);
        // auth_methods: comma-separated
        auto methods_str = fields[1];
        for (auto& m : split_by(methods_str, ',')) {
            std::string mt = trim(m);
            if (!mt.empty()) entry.auth_methods.push_back(mt);
        }
        entry.password_hash_hex = fields[2];
        entry.salt_hex          = fields[3];
        if (!fields[4].empty()) {
            try { entry.pbkdf2_iterations = std::stoi(fields[4]); }
            catch (...) { entry.pbkdf2_iterations = 200000; }
        }
        // mlkem_level
        try {
            if (!fields[5].empty()) {
                entry.mlkem_level = crypto::MlKem::string_to_level(fields[5]);
            }
        } catch (...) {
            entry.mlkem_level = crypto::MlKemLevel::MLKEM_768;
        }
        // mlkem_pubkey_b64
        if (!fields[6].empty()) {
            entry.mlkem_public_key = crypto::base64_decode(fields[6]);
        }
        // allowed_paths: comma-separated
        for (auto& p : split_by(fields[7], ',')) {
            std::string pt = trim(p);
            if (!pt.empty()) entry.allowed_paths.push_back(pt);
        }
        if (entry.allowed_paths.empty()) {
            entry.allowed_paths.push_back("*"); // default: all paths
        }

        if (!entry.username.empty()) {
            db.users_.push_back(std::move(entry));
        }
    }
    return db;
}

void UserDb::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) throw FileException("Cannot write user database: " + path);

    f << "# net_copy user database\n";
    f << "# Format: username;auth_methods;password_hash_hex;salt_hex;iterations;mlkem_level;mlkem_pubkey_b64;allowed_paths\n";
    f << "# auth_methods: comma-separated: password, mlkem\n";
    f << "# allowed_paths: comma-separated paths, or * for all paths\n";
    f << "# Use net_copy_admin to manage users:\n";
    f << "#   net_copy_admin adduser --users users.csv --name admin --pass YourPassword --paths *\n";

    for (const auto& u : users_) {
        // auth_methods
        std::string methods_str;
        for (size_t i = 0; i < u.auth_methods.size(); ++i) {
            if (i > 0) methods_str += ',';
            methods_str += u.auth_methods[i];
        }
        // mlkem_pubkey_b64
        std::string pubkey_b64 = u.mlkem_public_key.empty() ? "" : crypto::base64_encode(u.mlkem_public_key);
        // allowed_paths
        std::string paths_str;
        for (size_t i = 0; i < u.allowed_paths.size(); ++i) {
            if (i > 0) paths_str += ',';
            paths_str += u.allowed_paths[i];
        }
        std::string mlkem_level_str = u.mlkem_public_key.empty() ? "" : crypto::MlKem::level_to_string(u.mlkem_level);

        f << escape_field(u.username) << ";"
          << methods_str << ";"
          << u.password_hash_hex << ";"
          << u.salt_hex << ";"
          << u.pbkdf2_iterations << ";"
          << mlkem_level_str << ";"
          << pubkey_b64 << ";"
          << paths_str << "\n";
    }
}

void UserDb::save() const {
    if (path_.empty()) throw FileException("UserDb path not set");
    save(path_);
}

bool UserDb::user_exists(const std::string& username) const {
    return find_user(username) != nullptr;
}

const UserEntry* UserDb::find_user(const std::string& username) const {
    for (const auto& u : users_) {
        if (u.username == username) return &u;
    }
    return nullptr;
}

UserEntry* UserDb::find_user_mutable(const std::string& username) {
    for (auto& u : users_) {
        if (u.username == username) return &u;
    }
    return nullptr;
}

void UserDb::add_user(const UserEntry& entry) {
    if (user_exists(entry.username)) {
        throw ConfigException("User already exists: " + entry.username);
    }
    users_.push_back(entry);
}

void UserDb::update_password(const std::string& username, const std::string& new_password) {
    auto* u = find_user_mutable(username);
    if (!u) throw ConfigException("User not found: " + username);

    auto salt = crypto::random_bytes(16);
    auto hash = crypto::pbkdf2_sha3_256(new_password, salt, 200000, 32);
    u->salt_hex           = crypto::bytes_to_hex(salt);
    u->password_hash_hex  = crypto::bytes_to_hex(hash);
    u->pbkdf2_iterations  = 200000;
}

void UserDb::update_public_key(const std::string& username,
                                const std::vector<uint8_t>& pubkey,
                                crypto::MlKemLevel level) {
    auto* u = find_user_mutable(username);
    if (!u) throw ConfigException("User not found: " + username);
    u->mlkem_public_key = pubkey;
    u->mlkem_level = level;
}

void UserDb::remove_user(const std::string& username) {
    auto it = std::find_if(users_.begin(), users_.end(),
                           [&](const UserEntry& e){ return e.username == username; });
    if (it == users_.end()) throw ConfigException("User not found: " + username);
    users_.erase(it);
}

bool UserDb::verify_password(const std::string& username, const std::string& password) const {
    const auto* u = find_user(username);
    if (!u) return false;
    if (u->salt_hex.empty() || u->password_hash_hex.empty()) return false;

    try {
        auto salt     = crypto::hex_to_bytes(u->salt_hex);
        auto stored   = crypto::hex_to_bytes(u->password_hash_hex);
        auto computed = crypto::pbkdf2_sha3_256(password, salt, u->pbkdf2_iterations, 32);

        if (computed.size() != stored.size()) return false;
        // Constant-time compare
        uint8_t diff = 0;
        for (size_t i = 0; i < computed.size(); ++i) {
            diff |= computed[i] ^ stored[i];
        }
        return diff == 0;
    } catch (...) {
        return false;
    }
}

} // namespace auth
} // namespace netcopy
