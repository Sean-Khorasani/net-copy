#include "crypto/crypto_engine.h"
#include <cstring>
#include <stdexcept>

namespace netcopy {
namespace crypto {

// HighSecurityEngine implementation
HighSecurityEngine::HighSecurityEngine(const std::string& password) 
    : nonce_counter_(0) {
    // Convert hex string to key (like the legacy implementation)
    std::string hex_key = password;
    if (hex_key.length() > 2 && hex_key.substr(0, 2) == "0x") {
        hex_key = hex_key.substr(2);
    }
    
    if (hex_key.length() != 64) {
        throw std::runtime_error("Invalid secret key length for HighSecurityEngine");
    }
    
    ChaCha20Poly1305::Key key;
    for (size_t i = 0; i < key.size(); ++i) {
        std::string byte_str = hex_key.substr(i * 2, 2);
        key[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
    
    cipher_ = std::make_unique<ChaCha20Poly1305>(key);
    
    // Initialize nonce
    current_nonce_.fill(0);
}

std::vector<uint8_t> HighSecurityEngine::encrypt(const std::vector<uint8_t>& data) {
    // Use exactly the same logic as the legacy implementation
    auto nonce = ChaCha20Poly1305::generate_nonce();
    auto encrypted = cipher_->encrypt(data, nonce);
    
    // Prepend nonce to encrypted data
    std::vector<uint8_t> result;
    result.insert(result.end(), nonce.begin(), nonce.end());
    result.insert(result.end(), encrypted.begin(), encrypted.end());
    
    return result;
}

std::vector<uint8_t> HighSecurityEngine::decrypt(const std::vector<uint8_t>& data) {
    // Use exactly the same logic as the legacy implementation
    if (data.size() < ChaCha20Poly1305::NONCE_SIZE + ChaCha20Poly1305::TAG_SIZE) {
        throw std::runtime_error("Encrypted message too short");
    }
    
    // Extract nonce
    ChaCha20Poly1305::Nonce nonce;
    std::copy(data.begin(), data.begin() + nonce.size(), nonce.begin());
    
    // Extract encrypted data (including tag)
    std::vector<uint8_t> encrypted_data(data.begin() + nonce.size(), data.end());
    
    // Extract tag from the end of encrypted data
    ChaCha20Poly1305::Tag tag;
    std::copy(encrypted_data.end() - tag.size(), encrypted_data.end(), tag.begin());
    
    return cipher_->decrypt(encrypted_data, nonce, tag);
}

void HighSecurityEngine::reset() {
    nonce_counter_ = 0;
    current_nonce_.fill(0);
}

void HighSecurityEngine::increment_nonce() {
    ++nonce_counter_;
    // Convert counter to bytes in little-endian format
    for (size_t i = 0; i < sizeof(nonce_counter_) && i < current_nonce_.size(); ++i) {
        current_nonce_[i] = static_cast<uint8_t>((nonce_counter_ >> (i * 8)) & 0xFF);
    }
}

// FastSecurityEngine implementation
FastSecurityEngine::FastSecurityEngine(const std::string& password) 
    : secret_key_(password) {
    // Parse hex key and create XorCipher
    std::string hex_key = password;
    if (hex_key.length() > 2 && hex_key.substr(0, 2) == "0x") {
        hex_key = hex_key.substr(2);
    }
    
    if (hex_key.length() != 64) {
        throw std::runtime_error("Invalid secret key length for FastSecurityEngine");
    }
    
    XorCipher::Key key;
    for (size_t i = 0; i < key.size(); ++i) {
        std::string byte_str = hex_key.substr(i * 2, 2);
        key[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
    
    cipher_ = std::make_unique<XorCipher>(key);
}

std::vector<uint8_t> FastSecurityEngine::encrypt(const std::vector<uint8_t>& data) {
    // Reset cipher state for each message to ensure deterministic encryption
    cipher_->reset();
    return cipher_->process(data);
}

std::vector<uint8_t> FastSecurityEngine::decrypt(const std::vector<uint8_t>& data) {
    // Reset cipher state for each message to ensure deterministic decryption
    cipher_->reset();
    return cipher_->process(data);
}

void FastSecurityEngine::reset() {
    // Reset the cipher state
    if (cipher_) {
        cipher_->reset();
    }
}

// AesSecurityEngine implementation
AesSecurityEngine::AesSecurityEngine(const std::string& password) 
    : secret_key_(password) {
    // Parse hex key and create AesCtr
    std::string hex_key = password;
    if (hex_key.length() > 2 && hex_key.substr(0, 2) == "0x") {
        hex_key = hex_key.substr(2);
    }
    
    if (hex_key.length() != 64) {
        throw std::runtime_error("Invalid secret key length for AesSecurityEngine");
    }
    
    AesCtr::Key key;
    for (size_t i = 0; i < key.size(); ++i) {
        std::string byte_str = hex_key.substr(i * 2, 2);
        key[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
    
    cipher_ = std::make_unique<AesCtr>(key);
}

std::vector<uint8_t> AesSecurityEngine::encrypt(const std::vector<uint8_t>& data) {
    // Generate a random IV for CTR mode
    auto iv = AesCtr::generate_iv();
    
    // Encrypt the data
    auto encrypted = cipher_->process(data, iv);
    
    // Prepend IV to encrypted data
    std::vector<uint8_t> result;
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), encrypted.begin(), encrypted.end());
    
    return result;
}

std::vector<uint8_t> AesSecurityEngine::decrypt(const std::vector<uint8_t>& data) {
    if (data.size() < AesCtr::IV_SIZE) {
        throw std::runtime_error("Encrypted data too short for AES-CTR");
    }
    
    // Extract IV from the beginning
    AesCtr::IV iv;
    std::copy(data.begin(), data.begin() + AesCtr::IV_SIZE, iv.begin());
    
    // Extract encrypted data
    std::vector<uint8_t> encrypted_data(data.begin() + AesCtr::IV_SIZE, data.end());
    
    // Decrypt (CTR mode encryption and decryption are the same)
    return cipher_->process(encrypted_data, iv);
}

void AesSecurityEngine::reset() {
    // Reset the cipher state
    if (cipher_) {
        cipher_->reset();
    }
}

std::string AesSecurityEngine::get_acceleration_info() const {
    return AesCtr::get_acceleration_info();
}

std::string AesSecurityEngine::get_detailed_acceleration_info() const {
    return AesCtr::get_detailed_acceleration_info();
}

bool AesSecurityEngine::is_using_hardware_acceleration() const {
    return cipher_ ? cipher_->is_using_hardware_acceleration() : false;
}

// GpuSecurityEngine implementation
GpuSecurityEngine::GpuSecurityEngine(const std::string& password) 
    : secret_key_(password), iv_counter_(0) {
    // Parse hex key and create Aes256GcmGpu
    std::string hex_key = password;
    if (hex_key.length() > 2 && hex_key.substr(0, 2) == "0x") {
        hex_key = hex_key.substr(2);
    }
    
    if (hex_key.length() != 64) {
        throw std::runtime_error("Invalid secret key length for GpuSecurityEngine");
    }
    
    Aes256GcmGpu::Key key;
    for (size_t i = 0; i < key.size(); ++i) {
        std::string byte_str = hex_key.substr(i * 2, 2);
        key[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
    }
    
    cipher_ = std::make_unique<Aes256GcmGpu>(key);
    
    // Initialize IV
    current_iv_.fill(0);
}

std::vector<uint8_t> GpuSecurityEngine::encrypt(const std::vector<uint8_t>& data) {
    // Generate a random IV for GCM mode
    auto iv = Aes256GcmGpu::generate_iv();
    
    // Encrypt the data (returns ciphertext + authentication tag)
    auto result = cipher_->encrypt(data, iv);
    
    // Prepend IV to encrypted data
    std::vector<uint8_t> final_result;
    final_result.insert(final_result.end(), iv.begin(), iv.end());
    final_result.insert(final_result.end(), result.begin(), result.end());
    
    return final_result;
}

std::vector<uint8_t> GpuSecurityEngine::decrypt(const std::vector<uint8_t>& data) {
    if (data.size() < Aes256GcmGpu::IV_SIZE + Aes256GcmGpu::TAG_SIZE) {
        throw std::runtime_error("Encrypted data too short for AES-256-GCM");
    }
    
    // Extract IV from the beginning
    Aes256GcmGpu::IV iv;
    std::copy(data.begin(), data.begin() + Aes256GcmGpu::IV_SIZE, iv.begin());
    
    // Extract encrypted data with tag
    std::vector<uint8_t> ciphertext_with_tag(data.begin() + Aes256GcmGpu::IV_SIZE, data.end());
    
    if (ciphertext_with_tag.size() < Aes256GcmGpu::TAG_SIZE) {
        throw std::runtime_error("Ciphertext too short for authentication tag");
    }
    
    // Extract tag from the end
    Aes256GcmGpu::Tag tag;
    std::copy(ciphertext_with_tag.end() - Aes256GcmGpu::TAG_SIZE, 
             ciphertext_with_tag.end(), tag.begin());
    
    // Extract ciphertext without tag
    std::vector<uint8_t> ciphertext(ciphertext_with_tag.begin(), 
                                   ciphertext_with_tag.end() - Aes256GcmGpu::TAG_SIZE);
    
    // Decrypt and verify
    return cipher_->decrypt(ciphertext, iv, tag);
}

void GpuSecurityEngine::reset() {
    iv_counter_ = 0;
    current_iv_.fill(0);
}

void GpuSecurityEngine::increment_iv() {
    ++iv_counter_;
    // Convert counter to bytes in little-endian format
    for (size_t i = 0; i < sizeof(iv_counter_) && i < current_iv_.size(); ++i) {
        current_iv_[i] = static_cast<uint8_t>((iv_counter_ >> (i * 8)) & 0xFF);
    }
}

std::string GpuSecurityEngine::get_acceleration_info() const {
    return Aes256GcmGpu::get_gpu_info();
}

std::string GpuSecurityEngine::get_detailed_acceleration_info() const {
    return Aes256GcmGpu::get_detailed_gpu_info();
}

bool GpuSecurityEngine::is_using_gpu_acceleration() const {
    return cipher_ ? cipher_->is_using_gpu_acceleration() : false;
}

Aes256GcmGpu::GpuMetrics GpuSecurityEngine::get_performance_metrics() const {
    return cipher_ ? cipher_->get_performance_metrics() : Aes256GcmGpu::GpuMetrics{};
}

// Factory function
std::unique_ptr<CryptoEngine> create_crypto_engine(SecurityLevel level, const std::string& password) {
    switch (level) {
        case SecurityLevel::HIGH:
            return std::make_unique<HighSecurityEngine>(password);
        case SecurityLevel::FAST:
            return std::make_unique<FastSecurityEngine>(password);
        case SecurityLevel::AES:
            return std::make_unique<AesSecurityEngine>(password);
        case SecurityLevel::AES_256_GCM:
            return std::make_unique<GpuSecurityEngine>(password);
        default:
            throw std::runtime_error("Unsupported security level");
    }
}

} // namespace crypto
} // namespace netcopy