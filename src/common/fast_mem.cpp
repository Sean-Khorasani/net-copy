#include "common/fast_mem.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fast_mem {

CpuFeatures detect_cpu_features() {
    CpuFeatures features = CpuFeatures::None;

#if defined(_MSC_VER)
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    if (cpuInfo[3] & (1 << 26)) features = static_cast<CpuFeatures>(static_cast<int>(features) | static_cast<int>(CpuFeatures::SSE2));

    __cpuid(cpuInfo, 7);
    if (cpuInfo[1] & (1 << 5)) features = static_cast<CpuFeatures>(static_cast<int>(features) | static_cast<int>(CpuFeatures::AVX2));
    if (cpuInfo[1] & (1 << 16)) features = static_cast<CpuFeatures>(static_cast<int>(features) | static_cast<int>(CpuFeatures::AVX512F));

#elif defined(__GNUC__) || defined(__clang__)
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        if (edx & (1 << 26)) features = static_cast<CpuFeatures>(static_cast<int>(features) | static_cast<int>(CpuFeatures::SSE2));
    }
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        if (ebx & (1 << 5)) features = static_cast<CpuFeatures>(static_cast<int>(features) | static_cast<int>(CpuFeatures::AVX2));
        if (ebx & (1 << 16)) features = static_cast<CpuFeatures>(static_cast<int>(features) | static_cast<int>(CpuFeatures::AVX512F));
    }
#endif
    return features;
}

void fast_memcpy(void* dest, const void* src, size_t n) {
    static CpuFeatures features = detect_cpu_features();
    if (static_cast<int>(features) & static_cast<int>(CpuFeatures::AVX2)) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
        fast_memcpy_avx2_masm(dest, src, n);
        return;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
        fast_memcpy_avx2(dest, src, n);
        return;
#endif
    }
    std::memcpy(dest, src, n);
}

void fast_memset(void* dest, int value, size_t n) {
    static CpuFeatures features = detect_cpu_features();
    if (static_cast<int>(features) & static_cast<int>(CpuFeatures::AVX2)) {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64))
        fast_memset_avx2_masm(dest, value, n);
        return;
#endif
    }
    std::memset(dest, value, n);
}

// ArenaAllocator Implementation
ArenaAllocator::ArenaAllocator(size_t size_bytes, size_t alignment)
    : capacity_(size_bytes), current_offset_(0), alignment_(alignment) {
#if defined(_WIN32)
    memory_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, size_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    memory_ = static_cast<uint8_t*>(mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (memory_ == MAP_FAILED) memory_ = nullptr;
#endif
    if (!memory_) {
        throw std::bad_alloc();
    }
}

ArenaAllocator::~ArenaAllocator() {
#if defined(_WIN32)
    if (memory_) VirtualFree(memory_, 0, MEM_RELEASE);
#else
    if (memory_) munmap(memory_, capacity_);
#endif
}

void* ArenaAllocator::allocate(size_t size, size_t alignment) {
    if (alignment == 0) alignment = alignment_;
    size_t aligned_offset = (current_offset_ + alignment - 1) & ~(alignment - 1);
    if (aligned_offset + size > capacity_) {
        return nullptr; // Or throw / fallback
    }
    void* ptr = memory_ + aligned_offset;
    current_offset_ = aligned_offset + size;
    return ptr;
}

void ArenaAllocator::reset() {
    current_offset_ = 0;
}

bool ArenaAllocator::owns(void* ptr) const {
    return ptr >= memory_ && ptr < memory_ + capacity_;
}

// PoolAllocator - Simple lock-free-ish free list (basic version)
PoolAllocator::PoolAllocator(size_t block_size, size_t num_blocks, size_t alignment)
    : block_size_(block_size), num_blocks_(num_blocks) {
    size_t total_size = block_size * num_blocks;
#if defined(_WIN32)
    memory_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
    memory_ = static_cast<uint8_t*>(mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
#endif
    if (!memory_) throw std::bad_alloc();

    // Initialize free list
    for (size_t i = 0; i < num_blocks; ++i) {
        void* block = memory_ + i * block_size;
        // Simple linked list using the memory itself
        *reinterpret_cast<void**>(block) = free_list_head_.load(std::memory_order_relaxed);
        free_list_head_.store(block, std::memory_order_relaxed);
    }
}

// Destructor
PoolAllocator::~PoolAllocator() {
#if defined(_WIN32)
    if (memory_) VirtualFree(memory_, 0, MEM_RELEASE);
#else
    if (memory_) munmap(memory_, block_size_ * num_blocks_);
#endif
}

void* PoolAllocator::allocate() {
    void* head = free_list_head_.load(std::memory_order_acquire);
    while (head) {
        void* next = *reinterpret_cast<void**>(head);
        if (free_list_head_.compare_exchange_weak(head, next, std::memory_order_acq_rel)) {
            return head;
        }
    }
    return nullptr; // Pool exhausted
}

void PoolAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    void* head = free_list_head_.load(std::memory_order_relaxed);
    do {
        *reinterpret_cast<void**>(ptr) = head;
    } while (!free_list_head_.compare_exchange_weak(head, ptr, std::memory_order_release));
}

// FastMemoryResource
FastMemoryResource::FastMemoryResource(size_t arena_size) : arena_(arena_size) {}

FastMemoryResource::~FastMemoryResource() = default;

void* FastMemoryResource::do_allocate(size_t bytes, size_t alignment) {
    return arena_.allocate(bytes, alignment);
}

void FastMemoryResource::do_deallocate(void* p, size_t bytes, size_t alignment) {
    // Arena doesn't support individual dealloc; ignore or assert owns(p)
    (void)p; (void)bytes; (void)alignment;
}

bool FastMemoryResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept {
    return this == &other;
}

// Thread-local arena helper
ArenaAllocator& get_thread_local_arena(size_t size) {
    thread_local ArenaAllocator arena(size);
    return arena;
}

} // namespace fast_mem
