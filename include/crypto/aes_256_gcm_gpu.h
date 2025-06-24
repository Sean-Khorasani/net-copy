#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <memory>

namespace netcopy {
namespace crypto {

class Aes256GcmGpu {
public:
    static constexpr size_t KEY_SIZE = 32;  // AES-256
    static constexpr size_t IV_SIZE = 12;   // GCM standard IV size
    static constexpr size_t TAG_SIZE = 16;  // GCM authentication tag size

    using Key = std::array<uint8_t, KEY_SIZE>;
    using IV = std::array<uint8_t, IV_SIZE>;
    using Tag = std::array<uint8_t, TAG_SIZE>;

    Aes256GcmGpu(const Key& key);
    ~Aes256GcmGpu();

    // GPU acceleration detection and information
    static bool is_gpu_acceleration_available();
    static std::string get_gpu_info();
    static std::string get_detailed_gpu_info();
    
    // Check if this instance is using GPU acceleration
    bool is_using_gpu_acceleration() const;
    
    // Encrypt data with authentication using GPU
    std::vector<uint8_t> encrypt(const std::vector<uint8_t>& plaintext, 
                                const IV& iv,
                                const std::vector<uint8_t>& additional_data = {});

    // Decrypt and verify data using GPU
    std::vector<uint8_t> decrypt(const std::vector<uint8_t>& ciphertext,
                                const IV& iv,
                                const Tag& tag,
                                const std::vector<uint8_t>& additional_data = {});

    // Generate random key
    static Key generate_key();
    
    // Generate random IV
    static IV generate_iv();

    // Get GPU performance metrics
    struct GpuMetrics {
        double encryption_throughput_mbps = 0.0;
        double decryption_throughput_mbps = 0.0;
        uint32_t gpu_memory_used_mb = 0;
        std::string gpu_device_name;
        int compute_capability_major = 0;
        int compute_capability_minor = 0;
    };
    
    GpuMetrics get_performance_metrics() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    bool gpu_available_;
};

// GPU device information structure
struct GpuDeviceInfo {
    int device_id = -1;
    std::string name;
    size_t total_memory = 0;
    size_t free_memory = 0;
    int compute_capability_major = 0;
    int compute_capability_minor = 0;
    int multiprocessor_count = 0;
    int max_threads_per_block = 0;
    bool supports_unified_memory = false;
};

// GPU detection utilities
class GpuUtils {
public:
    static bool is_cuda_available();
    static std::vector<GpuDeviceInfo> get_available_devices();
    static GpuDeviceInfo get_best_device_for_crypto();
    static std::string get_cuda_version();
    static bool check_compute_capability(int major, int minor);
};

} // namespace crypto
} // namespace netcopy