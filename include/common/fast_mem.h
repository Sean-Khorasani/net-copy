#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory_resource>
#include <vector>
#include <mutex>
#include <new>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#include <immintrin.h>
#endif

namespace fast_mem {

// CPU Feature Detection
enum class CpuFeatures {
    None = 0,
    SSE2 = 1 << 0,
    AVX2 = 1 << 1,
    AVX512F = 1 << 2,
    AVX512BW = 1 << 3
};

CpuFeatures detect_cpu_features();

// GCC/Clang GAS assembly (Linux/macOS)
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
extern "C" void* fast_memcpy_avx2(void* dest, const void* src, size_t n);
#endif

// Fast memory operations with best available SIMD
void fast_memcpy(void* dest, const void* src, size_t n);
void fast_memset(void* dest, int value, size_t n);

// Arena Allocator - Perfect for file chunks and session buffers
// Pre-allocates one large block. Bump allocation. O(1) alloc, instant reset.
class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t size_bytes, size_t alignment = 64); // 64-byte align default for SIMD/crypto
    ~ArenaAllocator();

    void* allocate(size_t size, size_t alignment = 0);
    void reset(); // Instant "free all" - no individual dealloc needed

    size_t used() const { return current_offset_; }
    size_t capacity() const { return capacity_; }
    bool owns(void* ptr) const;

private:
    uint8_t* memory_;
    size_t capacity_;
    size_t current_offset_;
    size_t alignment_;
    std::atomic<bool> in_use_{false}; // Basic thread safety hint
};

// Pool Allocator for fixed-size objects (e.g. packet buffers)
class PoolAllocator {
public:
    PoolAllocator(size_t block_size, size_t num_blocks, size_t alignment = 64);
    ~PoolAllocator();

    void* allocate();
    void deallocate(void* ptr);

    size_t block_size() const { return block_size_; }

private:
    uint8_t* memory_;
    size_t block_size_;
    size_t num_blocks_;
    std::vector<void*> free_list_;
    std::mutex mutex_;
};

// std::pmr compatible memory resource
class FastMemoryResource : public std::pmr::memory_resource {
public:
    explicit FastMemoryResource(size_t arena_size = 128 * 1024 * 1024); // 128MB default
    ~FastMemoryResource() override;

protected:
    void* do_allocate(size_t bytes, size_t alignment) override;
    void do_deallocate(void* p, size_t bytes, size_t alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

private:
    ArenaAllocator arena_;
};

// Helper to get a thread-local arena (great for multi-threaded transfer)
ArenaAllocator& get_thread_local_arena(size_t size = 64 * 1024 * 1024);

} // namespace fast_mem
