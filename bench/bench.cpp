// Compares FastAlloc against glibc malloc/free under multi-threaded churn.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

#include "fastalloc.hpp"

using Clock = std::chrono::steady_clock;
constexpr int kOpsPerThread = 5'000'000;
constexpr int kWindow = 1024;  // live allocations per thread

template <class Alloc, class Free>
double run(int threads, Alloc alloc, Free dealloc) {
    auto worker = [&](int seed) {
        std::mt19937 rng(seed);
        std::vector<char*> live(kWindow, nullptr);
        for (int i = 0; i < kOpsPerThread; ++i) {
            std::size_t slot = rng() % kWindow;
            if (live[slot]) dealloc(live[slot]);
            std::size_t n = 16 + rng() % 1009;  // 16..1024 bytes
            char* p = static_cast<char*>(alloc(n));
            p[0] = 1; p[n - 1] = 1;  // touch memory so the work is not optimised away
            live[slot] = p;
        }
        for (char* p : live) if (p) dealloc(p);
    };
    auto t0 = Clock::now();
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t) ts.emplace_back(worker, t + 1);
    for (auto& t : ts) t.join();
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    return static_cast<double>(threads) * kOpsPerThread / secs / 1e6;  // million allocs/sec
}

int main() {
    std::printf("%-8s %16s %16s %10s\n", "threads", "malloc (Mops/s)", "fastalloc (Mops/s)", "speedup");
    for (int t : {1, 2, 4, 8}) {
        double m = run(t, [](std::size_t n) { return std::malloc(n); }, [](void* p) { std::free(p); });
        double f = run(t, [](std::size_t n) { return fastalloc::allocate(n); }, [](void* p) { fastalloc::deallocate(p); });
        std::printf("%-8d %16.1f %16.1f %9.2fx\n", t, m, f, f / m);
    }
}
