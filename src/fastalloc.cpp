#include "fastalloc.hpp"

#include <sys/mman.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>

namespace fastalloc {
namespace {

// ---- Tunables -------------------------------------------------------------
constexpr std::size_t kHeader     = 16;        // keeps user pointers 16B aligned
constexpr std::size_t kChunkSize  = 1u << 20;  // 1 MiB mmap'd from the OS at a time
constexpr std::size_t kBatch      = 64;        // blocks moved thread cache <-> central
constexpr std::size_t kMaxLocal   = 256;       // thread cache limit per size class
constexpr std::size_t kClassSizes[] = {16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024};
constexpr std::size_t kNumClasses = sizeof(kClassSizes) / sizeof(kClassSizes[0]);
constexpr std::size_t kMaxSmall   = 1024;
constexpr std::uint64_t kLargeTag = ~0ull;

// Every block is preceded by a header so free() can find its size class in O(1).
struct Header   { std::uint64_t cls; std::uint64_t unused; };
struct FreeNode { FreeNode* next; };  // intrusive list, lives inside the free block

// Lookup table: (size+15)/16 -> size class index.
struct ClassTable {
    std::uint8_t idx[kMaxSmall / 16 + 1];
    constexpr ClassTable() : idx{} {
        std::size_t c = 0;
        for (std::size_t i = 0; i <= kMaxSmall / 16; ++i) {
            while (kClassSizes[c] < i * 16) ++c;
            idx[i] = static_cast<std::uint8_t>(c);
        }
    }
};
constexpr ClassTable kTable{};
inline std::size_t class_for(std::size_t n) { return kTable.idx[(n + 15) / 16]; }

// ---- Central heap: shared, one lock per size class ------------------------
struct Central {
    std::mutex m;
    FreeNode* free_list = nullptr;  // blocks returned by threads
    char* bump = nullptr;           // unused part of current chunk
    char* bump_end = nullptr;
};
Central g_central[kNumClasses];
std::atomic<std::size_t> g_chunks{0};
std::atomic<std::size_t> g_large{0};

// Take up to `want` blocks from central. Returns count, list via `out`.
std::size_t central_fetch(std::size_t cls, std::size_t want, FreeNode*& out) {
    Central& c = g_central[cls];
    const std::size_t stride = kHeader + kClassSizes[cls];
    std::lock_guard<std::mutex> lock(c.m);
    FreeNode* head = nullptr;
    std::size_t got = 0;
    while (got < want && c.free_list) {            // 1) reuse freed blocks
        FreeNode* n = c.free_list;
        c.free_list = n->next;
        n->next = head; head = n; ++got;
    }
    while (got < want) {                           // 2) carve fresh blocks
        if (c.bump == nullptr || c.bump + stride > c.bump_end) {
            void* mem = mmap(nullptr, kChunkSize, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mem == MAP_FAILED) break;
            g_chunks.fetch_add(1, std::memory_order_relaxed);
            c.bump = static_cast<char*>(mem);
            c.bump_end = c.bump + kChunkSize;
        }
        auto* h = reinterpret_cast<Header*>(c.bump);
        h->cls = cls;
        auto* n = reinterpret_cast<FreeNode*>(c.bump + kHeader);
        c.bump += stride;
        n->next = head; head = n; ++got;
    }
    out = head;
    return got;
}

void central_release(std::size_t cls, FreeNode* head, FreeNode* tail) {
    Central& c = g_central[cls];
    std::lock_guard<std::mutex> lock(c.m);
    tail->next = c.free_list;
    c.free_list = head;
}

// ---- Per-thread cache: the lock-free fast path ----------------------------
thread_local bool t_dead = false;  // trivially destructible, stays valid at thread exit

struct ThreadCache {
    FreeNode* head[kNumClasses] = {};
    std::uint32_t count[kNumClasses] = {};

    ~ThreadCache() {
        t_dead = true;
        for (std::size_t c = 0; c < kNumClasses; ++c) {
            if (!head[c]) continue;
            FreeNode* tail = head[c];
            while (tail->next) tail = tail->next;
            central_release(c, head[c], tail);
            head[c] = nullptr; count[c] = 0;
        }
    }
};
thread_local ThreadCache t_cache;

}  // namespace

void* allocate(std::size_t n) noexcept {
    if (n == 0) n = 1;
    if (n > kMaxSmall) {  // large: defer to malloc, tag the header
        if (n > SIZE_MAX - kHeader) return nullptr;
        void* raw = std::malloc(n + kHeader);
        if (!raw) return nullptr;
        static_cast<Header*>(raw)->cls = kLargeTag;
        g_large.fetch_add(1, std::memory_order_relaxed);
        return static_cast<char*>(raw) + kHeader;
    }
    const std::size_t cls = class_for(n);
    if (t_dead) {  // thread is shutting down: bypass the cache
        FreeNode* l = nullptr;
        if (central_fetch(cls, 1, l) == 0) return nullptr;
        return l;
    }
    ThreadCache& tc = t_cache;
    if (!tc.head[cls]) {  // slow path: refill from central heap
        FreeNode* batch = nullptr;
        std::size_t got = central_fetch(cls, kBatch, batch);
        if (got == 0) return nullptr;
        tc.head[cls] = batch;
        tc.count[cls] = static_cast<std::uint32_t>(got);
    }
    FreeNode* node = tc.head[cls];  // fast path: pop, no locks, no atomics
    tc.head[cls] = node->next;
    --tc.count[cls];
    return node;
}

void deallocate(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<Header*>(static_cast<char*>(p) - kHeader);
    if (h->cls == kLargeTag) { std::free(h); return; }
    const std::size_t cls = static_cast<std::size_t>(h->cls);
    auto* node = static_cast<FreeNode*>(p);
    if (t_dead) { central_release(cls, node, node); return; }

    ThreadCache& tc = t_cache;
    node->next = tc.head[cls];
    tc.head[cls] = node;
    if (++tc.count[cls] > kMaxLocal) {  // cache too big: give half back to central
        const std::size_t give = kMaxLocal / 2;
        FreeNode* first = tc.head[cls];
        FreeNode* last = first;
        for (std::size_t i = 1; i < give; ++i) last = last->next;
        tc.head[cls] = last->next;
        tc.count[cls] -= static_cast<std::uint32_t>(give);
        central_release(cls, first, last);
    }
}

Stats stats() noexcept {
    return {g_chunks.load(std::memory_order_relaxed), g_large.load(std::memory_order_relaxed)};
}

}  // namespace fastalloc
