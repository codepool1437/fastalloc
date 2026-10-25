# FastAlloc

A thread-caching, size-class memory allocator for Linux, written in C++17.
Inspired by the design of tcmalloc / jemalloc. Small allocations are served from
per-thread free lists **without taking any lock**; only refills and flushes touch shared state.

## Design

```
 allocate(n)                                   deallocate(p)
     |                                               |
     v                                               v
 size class lookup (O(1) table)              read 16B header -> size class
     |                                               |
     v                                               v
 +------------------+   empty: batch of 64   +---------------------+
 | Thread cache     | <--------------------- | Central heap        |
 | (per-thread free |                        | (mutex per class)   |
 |  list per class) | ---------------------> | free list + bump    |
 +------------------+   >256 cached: return  | allocator over 1MiB |
                        half to central       | mmap'd chunks       |
                                              +---------------------+
 n > 1024 B  ->  malloc fallback (tagged header)
```

- **12 size classes** (16 B to 1 KiB), O(1) class lookup via a `constexpr` table.
- **16-byte header** per block stores the size class, so `free()` needs no size argument
  and returned pointers stay 16-byte aligned.
- **Intrusive free lists**: a free block stores the `next` pointer inside itself (zero metadata overhead).
- **Batching**: threads move 64 blocks at a time between cache and central heap, so lock cost is amortised.
- **Cross-thread frees** are supported; thread exit flushes the cache back to the central heap.
- **Bump allocation** inside 1 MiB `mmap` chunks avoids per-block syscalls.
- Drop-in `fastalloc::Allocator<T>` for STL containers.

## Build and run

```bash
make test     # correctness tests (alignment, overlap, integrity, threads, cross-thread free)
make tsan     # same tests under ThreadSanitizer
make bench    # compare against glibc malloc
```

CMake also works: `cmake -B build && cmake --build build`.

## Benchmark

Each thread keeps 1024 live allocations of random size (16 to 1024 B) and repeatedly
frees one and allocates another (5M ops/thread).

| Threads | malloc (Mops/s) | FastAlloc (Mops/s) | Speedup |
|---------|-----------------|--------------------|---------|
| 1       | _run `make bench` and paste your numbers_ | | |
| 2       | | | |
| 4       | | | |
| 8       | | | |

## Limitations / future work

- Chunks are never returned to the OS (no `munmap`).
- Large allocations (>1 KiB) fall back to `malloc` rather than a page-level allocator.
- Linux only (`mmap`).
- Possible next steps: per-thread-cache size adaptation, page-level span allocator for large sizes,
  NUMA awareness, and an `LD_PRELOAD` shim to replace `malloc` in existing programs.
