#pragma once

#include "crypto/chacha20_poly1305.h"
#include "crypto/xor_cipher.h"
#include "crypto/aes_ctr.h"
#include "crypto/aes_256_gcm_gpu.h"
#include <memory>
#include <vector>
#include <cstdint>

namespace netcopy {
namespace crypto {

// Abstract interface for encryption/decryption
class CryptoEngine {
public:
    virtual ~CryptoEngine() = default;
    
    virtual std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) = 0;
    
    virtual SecurityLevel get_security_level() const = 0;
    virtual void reset() = 0;
};

// ChaCha20-Poly1305 implementation
class HighSecurityEngine : public CryptoEngine {
public:
    explicit HighSecurityEngine(const std::string& password);
    
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) override;
    
    SecurityLevel get_security_level() const override { return SecurityLevel::HIGH; }
    void reset() override;

private:
    std::unique_ptr<ChaCha20Poly1305> cipher_;
    ChaCha20Poly1305::Nonce current_nonce_;
    uint64_t nonce_counter_;
    
    void increment_nonce();
};

// XOR cipher implementation
class FastSecurityEngine : public CryptoEngine {
public:
    explicit FastSecurityEngine(const std::string& password);
    
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) override;
    
    SecurityLevel get_security_level() const override { return SecurityLevel::FAST; }
    void reset() override;

private:
    std::unique_ptr<XorCipher> cipher_;
    std::string secret_key_;
};

// AES-CTR cipher implementation with hardware acceleration
class AesSecurityEngine : public CryptoEngine {
public:
    explicit AesSecurityEngine(const std::string& password);
    
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) override;
    
    SecurityLevel get_security_level() const override { return SecurityLevel::AES; }
    void reset() override;
    
    // Get hardware acceleration info
    std::string get_acceleration_info() const;
    std::string get_detailed_acceleration_info() const;
    bool is_using_hardware_acceleration() const;

private:
    std::unique_ptr<AesCtr> cipher_;
    std::string secret_key_;
};

// AES-256-GCM cipher implementation with GPU acceleration
class GpuSecurityEngine : public CryptoEngine {
public:
    explicit GpuSecurityEngine(const std::string& password);
    
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& data) override;
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& data) override;
    
    SecurityLevel get_security_level() const override { return SecurityLevel::AES_256_GCM; }
    void reset() override;
    
    // Get GPU acceleration info
    std::string get_acceleration_info() const;
    std::string get_detailed_acceleration_info() const;
    bool is_using_gpu_acceleration() const;
    Aes256GcmGpu::GpuMetrics get_performance_metrics() const;

private:
    std::unique_ptr<Aes256GcmGpu> cipher_;
    std::string secret_key_;
    Aes256GcmGpu::IV current_iv_;
    uint64_t iv_counter_;
    
    void increment_iv();
};

// Factory function
std::unique_ptr<CryptoEngine> create_crypto_engine(SecurityLevel level, const std::string& password);

} // namespace crypto
} // namespace netcopy