/*
 * bench_heap.c — heap allocator benchmarks.
 *
 * Measures: alloc+free throughput, batch patterns, bucket sizes,
 * large allocations, realloc, zeroed alloc.
 */

#include "bench.h"
#include <lightc/heap.h>

/* --- Single alloc+free pairs --- */

static void bench_alloc_free_16(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate(16);
        *(volatile char *)p = 0;
        lc_heap_free(p);
    }
}

static void bench_alloc_free_64(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate(64);
        *(volatile char *)p = 0;
        lc_heap_free(p);
    }
}

static void bench_alloc_free_256(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate(256);
        *(volatile char *)p = 0;
        lc_heap_free(p);
    }
}

static void bench_alloc_free_2048(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate(2048);
        *(volatile char *)p = 0;
        lc_heap_free(p);
    }
}

/* --- Large allocations (mmap path) --- */

static void bench_alloc_free_large(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate(8192);
        *(volatile char *)p = 0;
        lc_heap_free(p);
    }
}

/* --- Zeroed allocation --- */

static void bench_alloc_zeroed_64(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate_zeroed(64);
        *(volatile char *)p;
        lc_heap_free(p);
    }
}

/* --- Realloc (grow) --- */

static void bench_realloc_grow(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_heap_allocate(32);
        p = lc_heap_reallocate(p, 128);
        p = lc_heap_reallocate(p, 512);
        lc_heap_free(p);
    }
}

/* --- Batch: allocate N then free N --- */

#define BATCH 256

static void bench_batch_64(bench_state *b) {
    void *ptrs[BATCH];
    for (int64_t i = 0; i < b->iterations; i++) {
        for (int j = 0; j < BATCH; j++)
            ptrs[j] = lc_heap_allocate(64);
        for (int j = 0; j < BATCH; j++)
            lc_heap_free(ptrs[j]);
    }
}

/* --- Mixed sizes --- */

static void bench_mixed_sizes(bench_state *b) {
    static const size_t sizes[] = {16, 64, 100, 200, 500, 1000, 2048};
    void *ptrs[7];
    for (int64_t i = 0; i < b->iterations; i++) {
        for (int j = 0; j < 7; j++)
            ptrs[j] = lc_heap_allocate(sizes[j]);
        for (int j = 0; j < 7; j++)
            lc_heap_free(ptrs[j]);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    BENCH_SUITE("heap: alloc + free pairs");
    BENCH_RUN("16 bytes",   bench_alloc_free_16);
    BENCH_RUN("64 bytes",   bench_alloc_free_64);
    BENCH_RUN("256 bytes",  bench_alloc_free_256);
    BENCH_RUN("2048 bytes", bench_alloc_free_2048);
    BENCH_RUN("8192 bytes (mmap)", bench_alloc_free_large);

    BENCH_SUITE("heap: zeroed + realloc");
    BENCH_RUN("alloc_zeroed 64B",  bench_alloc_zeroed_64);
    BENCH_RUN("realloc 32->128->512", bench_realloc_grow);

    BENCH_SUITE("heap: batch patterns");
    BENCH_RUN("batch 256x64B",  bench_batch_64);
    BENCH_RUN("mixed 7 sizes",  bench_mixed_sizes);

    return 0;
}
