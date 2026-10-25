// FastAlloc - a thread-caching, size-class memory allocator for Linux.
#pragma once
#include <cstddef>
#include <new>

namespace fastalloc {

// Allocate n bytes, 16-byte aligned. Returns nullptr on failure.
void* allocate(std::size_t n) noexcept;

// Free memory obtained from allocate(). nullptr is a no-op.
// May be called from a different thread than the one that allocated.
void deallocate(void* p) noexcept;

struct Stats {
    std::size_t chunks_mapped;  // 1 MiB chunks obtained from the OS via mmap
    std::size_t large_allocs;   // requests that bypassed size classes
};
Stats stats() noexcept;

// Drop-in std::allocator replacement: std::vector<int, fastalloc::Allocator<int>>
template <class T>
struct Allocator {
    using value_type = T;
    Allocator() = default;
    template <class U> Allocator(const Allocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = fastalloc::allocate(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept { fastalloc::deallocate(p); }
};
template <class T, class U> bool operator==(const Allocator<T>&, const Allocator<U>&) { return true; }
template <class T, class U> bool operator!=(const Allocator<T>&, const Allocator<U>&) { return false; }

}  // namespace fastalloc
