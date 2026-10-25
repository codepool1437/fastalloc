#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "fastalloc.hpp"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static void test_basic() {
    for (std::size_t n : {0u, 1u, 15u, 16u, 17u, 100u, 1024u, 1025u, 100000u}) {
        void* p = fastalloc::allocate(n);
        CHECK(p);
        CHECK(reinterpret_cast<std::uintptr_t>(p) % 16 == 0);
        std::memset(p, 0xAB, n ? n : 1);
        fastalloc::deallocate(p);
    }
    fastalloc::deallocate(nullptr);
    std::puts("  basic + alignment ok");
}

static void test_no_overlap_and_integrity() {
    std::mt19937 rng(1);
    struct Blk { unsigned char* p; std::size_t n; unsigned char tag; };
    std::vector<Blk> live;
    for (int i = 0; i < 50000; ++i) {
        std::size_t n = 1 + rng() % 1500;
        auto* p = static_cast<unsigned char*>(fastalloc::allocate(n));
        CHECK(p);
        unsigned char tag = static_cast<unsigned char>(i);
        std::memset(p, tag, n);
        live.push_back({p, n, tag});
    }
    for (auto& b : live)  // any overlap would have corrupted a neighbour's pattern
        for (std::size_t i = 0; i < b.n; ++i) CHECK(b.p[i] == b.tag);
    for (auto& b : live) fastalloc::deallocate(b.p);
    std::puts("  no overlap / data integrity ok");
}

static void test_reuse() {
    void* a = fastalloc::allocate(64);
    fastalloc::deallocate(a);
    void* b = fastalloc::allocate(64);
    CHECK(a == b);  // LIFO free list: the hot block is reused
    fastalloc::deallocate(b);
    std::puts("  block reuse ok");
}

static void test_threads() {
    const int kThreads = 8;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
        ts.emplace_back([t] {
            std::mt19937 rng(t);
            std::vector<std::pair<unsigned char*, std::size_t>> live;
            for (int i = 0; i < 200000; ++i) {
                if (live.size() < 500 || rng() % 2) {
                    std::size_t n = 1 + rng() % 1200;
                    auto* p = static_cast<unsigned char*>(fastalloc::allocate(n));
                    CHECK(p);
                    std::memset(p, t, n);
                    live.push_back({p, n});
                } else {
                    std::size_t k = rng() % live.size();
                    auto pr = live[k];
                    for (std::size_t j = 0; j < pr.second; ++j) CHECK(pr.first[j] == t);
                    fastalloc::deallocate(pr.first);
                    live[k] = live.back(); live.pop_back();
                }
            }
            for (auto& pr : live) fastalloc::deallocate(pr.first);
        });
    for (auto& th : ts) th.join();
    std::puts("  multi-threaded stress ok");
}

static void test_cross_thread_free() {
    std::vector<void*> blocks;
    std::thread producer([&] {
        for (int i = 0; i < 100000; ++i) blocks.push_back(fastalloc::allocate(48));
    });
    producer.join();  // producer exits -> its cache is flushed to the central heap
    std::thread consumer([&] { for (void* p : blocks) fastalloc::deallocate(p); });
    consumer.join();
    std::puts("  cross-thread free ok");
}

static void test_stl() {
    std::vector<int, fastalloc::Allocator<int>> v;
    for (int i = 0; i < 100000; ++i) v.push_back(i);
    long long sum = 0;
    for (int x : v) sum += x;
    CHECK(sum == 99999LL * 100000 / 2);
    std::puts("  STL allocator ok");
}

int main() {
    std::puts("Running FastAlloc tests");
    test_basic();
    test_no_overlap_and_integrity();
    test_reuse();
    test_threads();
    test_cross_thread_free();
    test_stl();
    auto s = fastalloc::stats();
    std::printf("All tests passed (chunks mapped: %zu, large allocs: %zu)\n", s.chunks_mapped, s.large_allocs);
}
