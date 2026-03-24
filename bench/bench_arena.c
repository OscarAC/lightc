/*
 * bench_arena.c — arena allocator benchmarks.
 *
 * Measures: bump allocation speed, reset+reuse, alignment overhead.
 */

#include "bench.h"
#include <lightc/memory.h>

/* --- Single allocations from arena --- */

static void bench_arena_alloc_16(bench_state *b) {
    lc_arena arena = lc_arena_create(b->iterations * 32);
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_arena_allocate(&arena, 16);
        *(volatile char *)p = 0;
    }
    lc_arena_destroy(&arena);
}

static void bench_arena_alloc_64(bench_state *b) {
    lc_arena arena = lc_arena_create(b->iterations * 80);
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_arena_allocate(&arena, 64);
        *(volatile char *)p = 0;
    }
    lc_arena_destroy(&arena);
}

/* --- Alloc with reset cycles --- */

static void bench_arena_alloc_reset(bench_state *b) {
    /* Allocate 1000 x 64B, reset, repeat */
    lc_arena arena = lc_arena_create(1000 * 80);
    for (int64_t i = 0; i < b->iterations; i++) {
        for (int j = 0; j < 1000; j++) {
            void *p = lc_arena_allocate(&arena, 64);
            *(volatile char *)p = 0;
        }
        lc_arena_reset(&arena);
    }
    lc_arena_destroy(&arena);
}

/* --- Custom alignment --- */

static void bench_arena_alloc_aligned(bench_state *b) {
    lc_arena arena = lc_arena_create(b->iterations * 80);
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_arena_allocate_aligned(&arena, 48, 64);
        *(volatile char *)p = 0;
    }
    lc_arena_destroy(&arena);
}

/* --- Raw page allocation --- */

static void bench_page_alloc_free(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        void *p = lc_allocate_pages(1).value;
        *(volatile char *)p = 0;
        lc_free_pages(p, 1);
    }
}

/* --- Arena vs heap comparison: many small allocs --- */

static void bench_arena_1000_small(bench_state *b) {
    lc_arena arena = lc_arena_create(1000 * 32);
    for (int64_t i = 0; i < b->iterations; i++) {
        for (int j = 0; j < 1000; j++) {
            void *p = lc_arena_allocate(&arena, 16);
            *(volatile char *)p = 0;
        }
        lc_arena_reset(&arena);
    }
    lc_arena_destroy(&arena);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    BENCH_SUITE("arena: bump allocation");
    BENCH_RUN("16 bytes",        bench_arena_alloc_16);
    BENCH_RUN("64 bytes",        bench_arena_alloc_64);
    BENCH_RUN("48B align-64",    bench_arena_alloc_aligned);

    BENCH_SUITE("arena: reset + reuse");
    BENCH_RUN("1000x64B + reset", bench_arena_alloc_reset);
    BENCH_RUN("1000x16B + reset", bench_arena_1000_small);

    BENCH_SUITE("raw pages");
    BENCH_RUN("mmap+munmap 1 page", bench_page_alloc_free);

    return 0;
}
