// net_copy_admin - Administration tool for net_copy users and keys
// Replaces net_copy_keygen with a comprehensive admin interface.
#include "auth/user_db.h"
#include "crypto/sha3.h"
#include "crypto/mlkem.h"
#include "crypto/key_manager.h"
#include "exceptions.h"

#include <iostream>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#  include <conio.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

// ============================================================
// Password prompt (no-echo)
// ============================================================

static std::string prompt_password(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string pw;
#ifdef _WIN32
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n') {
        if (ch == '\b') {
            if (!pw.empty()) { pw.pop_back(); std::cout << "\b \b" << std::flush; }
        } else if (ch >= 32) {
            pw += static_cast<char>(ch);
            std::cout << '*' << std::flush;
        }
    }
    std::cout << '\n';
#else
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::getline(std::cin, pw);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << '\n';
#endif
    return pw;
}

// ============================================================
// Argument parsing
// ============================================================

static std::map<std::string, std::string> parse_args(int argc, char* argv[], int start = 2) {
    std::map<std::string, std::string> args;
    for (int i = start; i < argc; ++i) {
        std::string a = argv[i];
        if (a.size() > 2 && a.substr(0, 2) == "--") {
            std::string key = a.substr(2);
            if (i + 1 < argc && argv[i+1][0] != '-') {
                args[key] = argv[++i];
            } else {
                args[key] = "1"; // flag
            }
        }
    }
    return args;
}

// ============================================================
// Command implementations
// ============================================================

static void cmd_keygen(const std::map<std::string, std::string>& args) {
    // Parse level
    int level_int = 768;
    auto it = args.find("level");
    if (it != args.end()) level_int = std::stoi(it->second);

    netcopy::crypto::MlKemLevel level;
    if (level_int == 512)       level = netcopy::crypto::MlKemLevel::MLKEM_512;
    else if (level_int == 1024) level = netcopy::crypto::MlKemLevel::MLKEM_1024;
    else                         level = netcopy::crypto::MlKemLevel::MLKEM_768;

    // Output stem
    it = args.find("out");
    if (it == args.end()) {
        std::cerr << "Error: --out STEM required\n";
        return;
    }
    std::string stem = it->second;

    // Passphrase
    std::string passphrase;
    bool encrypt = args.count("encrypt") > 0;
    if (args.count("passphrase")) {
        passphrase = args.at("passphrase");
    } else if (encrypt) {
        passphrase = prompt_password("Enter passphrase for private key: ");
        std::string confirm = prompt_password("Confirm passphrase: ");
        if (passphrase != confirm) {
            std::cerr << "Error: passphrases do not match\n";
            return;
        }
    }

    std::cout << "Generating " << netcopy::crypto::MlKem::level_to_string(level) << " key pair..." << std::flush;
    auto kp = netcopy::crypto::MlKem::generate_keypair(level);
    std::cout << " done.\n";

    netcopy::crypto::save_public_key(stem + ".pub", kp.public_key, level);
    netcopy::crypto::save_private_key(stem + ".pem", kp.private_key, level, passphrase);

    std::cout << "Public key:  " << stem << ".pub\n";
    std::cout << "Private key: " << stem << ".pem" << (encrypt ? " (encrypted)" : "") << "\n";
    std::cout << "Level: " << netcopy::crypto::MlKem::level_to_string(level) << "\n";
    std::cout << "Public key size:  " << kp.public_key.size() << " bytes\n";
    std::cout << "Private key size: " << kp.private_key.size() << " bytes\n";
}

static void cmd_showpubkey(const std::map<std::string, std::string>& args, const std::string& key_file) {
    std::string passphrase;
    if (args.count("passphrase")) passphrase = args.at("passphrase");

    netcopy::crypto::MlKemLevel level;
    auto privkey = netcopy::crypto::load_private_key(key_file, level, passphrase);
    std::string level_str = netcopy::crypto::MlKem::level_to_string(level);
    size_t pub_size = netcopy::crypto::MlKem::public_key_size(level);

    std::cout << "Private key file: " << key_file << "\n";
    std::cout << "Algorithm: " << level_str << "\n";
    std::cout << "Private key size: " << privkey.size() << " bytes\n";
    std::cout << "Public key size: " << pub_size << " bytes\n";
    if (privkey.size() >= pub_size) {
        std::vector<uint8_t> pubkey(privkey.begin(), privkey.begin() + pub_size);
        std::cout << "Public key (base64):\n" << netcopy::crypto::base64_encode(pubkey) << "\n";
    }
}

static void cmd_encrypt_key(const std::map<std::string, std::string>& args, const std::string& key_file) {
    std::string passphrase;
    if (args.count("passphrase")) passphrase = args.at("passphrase");
    else {
        passphrase = prompt_password("Enter new passphrase: ");
        std::string confirm = prompt_password("Confirm passphrase: ");
        if (passphrase != confirm) { std::cerr << "Passphrases do not match\n"; return; }
    }

    netcopy::crypto::MlKemLevel level;
    auto privkey = netcopy::crypto::load_private_key(key_file, level);

    std::string out = args.count("out") ? args.at("out") : key_file;
    netcopy::crypto::save_private_key(out, privkey, level, passphrase);
    std::cout << "Encrypted private key written to: " << out << "\n";
}

static void cmd_decrypt_key(const std::map<std::string, std::string>& args, const std::string& key_file) {
    std::string passphrase;
    if (args.count("passphrase")) passphrase = args.at("passphrase");
    else passphrase = prompt_password("Enter passphrase: ");

    netcopy::crypto::MlKemLevel level;
    auto privkey = netcopy::crypto::load_private_key(key_file, level, passphrase);

    std::string out = args.count("out") ? args.at("out") : key_file;
    netcopy::crypto::save_private_key(out, privkey, level, ""); // no passphrase = plain
    std::cout << "Decrypted private key written to: " << out << "\n";
}

static void cmd_adduser(const std::map<std::string, std::string>& args) {
    if (!args.count("users") || !args.count("name")) {
        std::cerr << "Error: --users and --name are required\n"; return;
    }
    std::string users_file = args.at("users");
    std::string name       = args.at("name");

    auto db = netcopy::auth::UserDb::load(users_file);

    if (db.user_exists(name)) {
        std::cerr << "Error: User already exists: " << name << "\n"; return;
    }

    netcopy::auth::UserEntry entry;
    entry.username = name;

    // Auth methods
    if (args.count("methods")) {
        std::string ms = args.at("methods");
        std::istringstream ss(ms);
        std::string m;
        while (std::getline(ss, m, ',')) {
            std::string mt = m;
            mt.erase(0, mt.find_first_not_of(" \t"));
            mt.erase(mt.find_last_not_of(" \t") + 1);
            if (!mt.empty()) entry.auth_methods.push_back(mt);
        }
    }
    if (entry.auth_methods.empty()) entry.auth_methods.push_back("password");

    // Password
    bool has_password_method = std::find(entry.auth_methods.begin(), entry.auth_methods.end(), "password") != entry.auth_methods.end();
    if (has_password_method) {
        std::string pass;
        if (args.count("pass") && args.at("pass") != "-") {
            pass = args.at("pass");
        } else {
            pass = prompt_password("Enter password for " + name + ": ");
            std::string confirm = prompt_password("Confirm password: ");
            if (pass != confirm) { std::cerr << "Passwords do not match\n"; return; }
        }
        auto salt = netcopy::crypto::random_bytes(16);
        auto hash = netcopy::crypto::pbkdf2_sha3_256(pass, salt, 200000, 32);
        entry.salt_hex           = netcopy::crypto::bytes_to_hex(salt);
        entry.password_hash_hex  = netcopy::crypto::bytes_to_hex(hash);
        entry.pbkdf2_iterations  = 200000;
    }

    // ML-KEM public key
    if (args.count("pubkey")) {
        netcopy::crypto::MlKemLevel level;
        entry.mlkem_public_key = netcopy::crypto::load_public_key(args.at("pubkey"), level);
        entry.mlkem_level = level;
        if (std::find(entry.auth_methods.begin(), entry.auth_methods.end(), "mlkem") == entry.auth_methods.end()) {
            entry.auth_methods.push_back("mlkem");
        }
    }

    // Allowed paths
    if (args.count("paths")) {
        std::string ps = args.at("paths");
        std::istringstream ss(ps);
        std::string p;
        while (std::getline(ss, p, ',')) {
            std::string pt = p;
            pt.erase(0, pt.find_first_not_of(" \t"));
            pt.erase(pt.find_last_not_of(" \t") + 1);
            if (!pt.empty()) entry.allowed_paths.push_back(pt);
        }
    }
    if (entry.allowed_paths.empty()) entry.allowed_paths.push_back("*");

    db.add_user(entry);
    db.save(users_file);
    std::cout << "User '" << name << "' added to " << users_file << "\n";
}

static void cmd_passwd(const std::map<std::string, std::string>& args) {
    if (!args.count("users") || !args.count("name")) {
        std::cerr << "Error: --users and --name are required\n"; return;
    }
    std::string users_file = args.at("users");
    std::string name       = args.at("name");

    auto db = netcopy::auth::UserDb::load(users_file);
    if (!db.user_exists(name)) {
        std::cerr << "Error: User not found: " << name << "\n"; return;
    }

    std::string pass;
    if (args.count("pass")) {
        pass = args.at("pass");
    } else {
        pass = prompt_password("New password for " + name + ": ");
        std::string confirm = prompt_password("Confirm: ");
        if (pass != confirm) { std::cerr << "Passwords do not match\n"; return; }
    }

    db.update_password(name, pass);
    db.save(users_file);
    std::cout << "Password updated for user '" << name << "'\n";
}

static void cmd_setkey(const std::map<std::string, std::string>& args) {
    if (!args.count("users") || !args.count("name") || !args.count("pubkey")) {
        std::cerr << "Error: --users, --name, --pubkey are required\n"; return;
    }
    std::string users_file = args.at("users");
    std::string name       = args.at("name");

    auto db = netcopy::auth::UserDb::load(users_file);
    if (!db.user_exists(name)) {
        std::cerr << "Error: User not found: " << name << "\n"; return;
    }

    netcopy::crypto::MlKemLevel level;
    auto pubkey = netcopy::crypto::load_public_key(args.at("pubkey"), level);
    db.update_public_key(name, pubkey, level);
    db.save(users_file);
    std::cout << "ML-KEM public key updated for user '" << name << "'\n";
}

static void cmd_deluser(const std::map<std::string, std::string>& args) {
    if (!args.count("users") || !args.count("name")) {
        std::cerr << "Error: --users and --name are required\n"; return;
    }
    std::string users_file = args.at("users");
    std::string name       = args.at("name");

    auto db = netcopy::auth::UserDb::load(users_file);
    if (!db.user_exists(name)) {
        std::cerr << "Error: User not found: " << name << "\n"; return;
    }
    db.remove_user(name);
    db.save(users_file);
    std::cout << "User '" << name << "' removed from " << users_file << "\n";
}

static void cmd_listusers(const std::map<std::string, std::string>& args) {
    if (!args.count("users")) {
        std::cerr << "Error: --users is required\n"; return;
    }
    auto db = netcopy::auth::UserDb::load(args.at("users"));
    if (!db.is_loaded()) {
        std::cout << "User database not found: " << args.at("users") << "\n"; return;
    }
    const auto& users = db.users();
    std::cout << "Users in " << args.at("users") << " (" << users.size() << " total):\n";
    std::cout << std::string(70, '-') << "\n";
    for (const auto& u : users) {
        std::string methods;
        for (size_t i = 0; i < u.auth_methods.size(); ++i) {
            if (i > 0) methods += ',';
            methods += u.auth_methods[i];
        }
        std::string paths;
        for (size_t i = 0; i < u.allowed_paths.size(); ++i) {
            if (i > 0) paths += ',';
            paths += u.allowed_paths[i];
        }
        std::cout << "  User:    " << u.username << "\n";
        std::cout << "  Methods: " << methods << "\n";
        std::cout << "  Paths:   " << paths << "\n";
        if (!u.mlkem_public_key.empty()) {
            std::cout << "  ML-KEM:  " << netcopy::crypto::MlKem::level_to_string(u.mlkem_level)
                      << " (" << u.mlkem_public_key.size() << " bytes)\n";
        }
        std::cout << std::string(70, '-') << "\n";
    }
}

static void print_usage() {
    std::cout << R"(net_copy_admin - Administration tool for net_copy

Usage:
  net_copy_admin keygen --level 512|768|1024 [--encrypt] [--passphrase PASS] --out STEM
      Generate ML-KEM key pair. Creates STEM.pub and STEM.pem.
      --encrypt: encrypt private key with passphrase (prompts if not given)

  net_copy_admin showpubkey KEY.pem [--passphrase PASS]
      Show public key information from a private key file

  net_copy_admin encrypt-key KEY.pem [--passphrase PASS] [--out OUT.pem]
      Encrypt a plain private key file

  net_copy_admin decrypt-key KEY.pem [--passphrase PASS] [--out OUT.pem]
      Decrypt an encrypted private key file

  net_copy_admin adduser --users USERS.CSV --name NAME [--pass PASS]
                         [--pubkey KEY.pub] [--paths PATH1,PATH2]
                         [--methods password,mlkem]
      Add a user to the database. Password prompts if omitted.

  net_copy_admin passwd --users USERS.CSV --name NAME [--pass NEWPASS]
      Change a user's password

  net_copy_admin setkey --users USERS.CSV --name NAME --pubkey KEY.pub
      Update a user's ML-KEM public key

  net_copy_admin deluser --users USERS.CSV --name NAME
      Remove a user from the database

  net_copy_admin listusers --users USERS.CSV
      List all users with their auth methods and paths
)";
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        print_usage();
        return 0;
    }

    std::string cmd = argv[1];
    auto args = parse_args(argc, argv);

    try {
        if (cmd == "keygen") {
            cmd_keygen(args);
        } else if (cmd == "showpubkey") {
            if (argc < 3) { std::cerr << "Error: KEY.pem argument required\n"; return 1; }
            cmd_showpubkey(args, argv[2]);
        } else if (cmd == "encrypt-key") {
            if (argc < 3) { std::cerr << "Error: KEY.pem argument required\n"; return 1; }
            cmd_encrypt_key(args, argv[2]);
        } else if (cmd == "decrypt-key") {
            if (argc < 3) { std::cerr << "Error: KEY.pem argument required\n"; return 1; }
            cmd_decrypt_key(args, argv[2]);
        } else if (cmd == "adduser") {
            cmd_adduser(args);
        } else if (cmd == "passwd") {
            cmd_passwd(args);
        } else if (cmd == "setkey") {
            cmd_setkey(args);
        } else if (cmd == "deluser") {
            cmd_deluser(args);
        } else if (cmd == "listusers") {
            cmd_listusers(args);
        } else {
            std::cerr << "Unknown command: " << cmd << "\n";
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
