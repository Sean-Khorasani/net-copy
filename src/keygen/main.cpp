#include "common/utils.h"
#include "crypto/chacha20_poly1305.h"
#include <iostream>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "-genkey") {
        std::cout << netcopy::common::get_version_string() << " - Key Generator" << std::endl;
        std::cout << netcopy::common::get_build_info() << std::endl << std::endl;
        
        try {
            std::string password = netcopy::common::get_password_from_console(
                "Please enter the master password to generate the secret key: ");
            
            if (password.empty()) {
                std::cerr << "Error: Password cannot be empty" << std::endl;
                return 1;
            }
            
            // Use fixed salt for consistent key generation
            // This ensures the same password always generates the same key
            std::vector<uint8_t> fixed_salt = {
                0x4e, 0x65, 0x74, 0x43, 0x6f, 0x70, 0x79, 0x53,
                0x61, 0x6c, 0x74, 0x31, 0x32, 0x33, 0x34, 0x35,
                0x36, 0x37, 0x38, 0x39, 0x30, 0x41, 0x42, 0x43,
                0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b
            };
            
            // Derive key from password
            auto key = netcopy::crypto::ChaCha20Poly1305::derive_key(password, fixed_salt);
            
            // Convert to hex string
            std::string key_hex = "0x" + netcopy::common::to_hex_string(
                std::vector<uint8_t>(key.begin(), key.end()));
            
            std::cout << "Insert the \"" << key_hex << "\" to your client and server configuration." << std::endl;
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }
    } else {
        std::cout << "NetCopy Key Generator" << std::endl;
        std::cout << "Usage: " << argv[0] << " -genkey" << std::endl;
        std::cout << "  -genkey    Generate a new encryption key from master password" << std::endl;
        return 1;
    }
    
    return 0;
}

