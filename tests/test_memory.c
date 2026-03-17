/*
 * test_memory.c — tests for lightc arena allocator and raw page allocation.
 */

#include "test.h"
#include <lightc/memory.h>

/* ===== lc_arena_create ===== */

static void test_arena_create(void) {
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);
    TEST_ASSERT(arena.capacity >= 4096);
    TEST_ASSERT_EQ(arena.used, (size_t)0);
    lc_arena_destroy(&arena);
}

/* ===== lc_arena_allocate ===== */

static void test_arena_allocate_basic(void) {
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);

    void *ptr = lc_arena_allocate(&arena, 64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write and read back */
    uint8_t *bytes = (uint8_t *)ptr;
    for (int i = 0; i < 64; i++) {
        bytes[i] = (uint8_t)i;
    }
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT_EQ(bytes[i], (uint8_t)i);
    }

    lc_arena_destroy(&arena);
}

/* ===== Arena alignment ===== */

static void test_arena_alignment(void) {
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);

    /* First allocation */
    void *p1 = lc_arena_allocate(&arena, 1);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_EQ((uintptr_t)p1 % LC_DEFAULT_ALIGNMENT, (uintptr_t)0);

    /* Second allocation — should still be 16-byte aligned */
    void *p2 = lc_arena_allocate(&arena, 1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_EQ((uintptr_t)p2 % LC_DEFAULT_ALIGNMENT, (uintptr_t)0);

    /* Third allocation with larger size */
    void *p3 = lc_arena_allocate(&arena, 33);
    TEST_ASSERT_NOT_NULL(p3);
    TEST_ASSERT_EQ((uintptr_t)p3 % LC_DEFAULT_ALIGNMENT, (uintptr_t)0);

    lc_arena_destroy(&arena);
}

/* ===== Multiple allocations ===== */

static void test_arena_multiple_allocations(void) {
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);

    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = lc_arena_allocate(&arena, 32);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }

    /* All pointers should be distinct */
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            TEST_ASSERT_NEQ(ptrs[i], ptrs[j]);
        }
    }

    /* Verify used space grew */
    TEST_ASSERT(lc_arena_get_used(&arena) > 0);

    lc_arena_destroy(&arena);
}

/* ===== lc_arena_reset ===== */

static void test_arena_reset(void) {
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);

    /* Allocate some memory */
    void *p1 = lc_arena_allocate(&arena, 128);
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT(lc_arena_get_used(&arena) > 0);

    /* Reset the arena */
    lc_arena_reset(&arena);
    TEST_ASSERT_EQ(lc_arena_get_used(&arena), (size_t)0);
    TEST_ASSERT_EQ(lc_arena_get_remaining(&arena), arena.capacity);

    /* Can allocate again from start */
    void *p2 = lc_arena_allocate(&arena, 128);
    TEST_ASSERT_NOT_NULL(p2);

    /* After reset, first allocation should start from base again */
    TEST_ASSERT_EQ(p2, (void *)arena.base);

    lc_arena_destroy(&arena);
}

/* ===== lc_arena_destroy ===== */

static void test_arena_destroy(void) {
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);

    lc_arena_destroy(&arena);

    /* After destroy, fields should be zeroed */
    TEST_ASSERT_NULL(arena.base);
    TEST_ASSERT_EQ(arena.capacity, (size_t)0);
    TEST_ASSERT_EQ(arena.used, (size_t)0);
}

/* ===== lc_allocate_pages / lc_free_pages ===== */

static void test_allocate_free_pages(void) {
    void *pages = lc_allocate_pages(2);
    TEST_ASSERT_NOT_NULL(pages);

    /* Write and read data across both pages */
    uint8_t *bytes = (uint8_t *)pages;
    for (int i = 0; i < 8192; i++) {
        bytes[i] = (uint8_t)(i & 0xFF);
    }
    for (int i = 0; i < 8192; i++) {
        TEST_ASSERT_EQ(bytes[i], (uint8_t)(i & 0xFF));
    }

    /* Free — should not crash */
    lc_free_pages(pages, 2);
}

/* ===== Arena overflow ===== */

static void test_arena_overflow(void) {
    /* Create a small arena (1 page = 4096 bytes) */
    lc_arena arena = lc_arena_create(4096);
    TEST_ASSERT_NOT_NULL(arena.base);

    /* Try to allocate more than capacity — should return NULL */
    void *ptr = lc_arena_allocate(&arena, 8192);
    TEST_ASSERT_NULL(ptr);

    /* Fill the arena up, then check the last one fails */
    lc_arena_reset(&arena);

    /* Allocate most of the arena */
    void *p1 = lc_arena_allocate(&arena, 4000);
    TEST_ASSERT_NOT_NULL(p1);

    /* Try to allocate more — should fail due to alignment padding + size */
    void *p2 = lc_arena_allocate(&arena, 200);
    TEST_ASSERT_NULL(p2);

    lc_arena_destroy(&arena);
}

/* ===== Arena Statistics ===== */

static void test_arena_stats(void) {
    lc_arena arena = lc_arena_create(4096);
    lc_arena_stats stats;

    /* Fresh arena — basic fields always available */
    lc_arena_get_stats(&arena, &stats);
    TEST_ASSERT_EQ(stats.used, (size_t)0);
    TEST_ASSERT(stats.capacity >= 4096);

    /* Allocate some */
    lc_arena_allocate(&arena, 100);
    lc_arena_allocate(&arena, 200);

    lc_arena_get_stats(&arena, &stats);
    TEST_ASSERT(stats.used >= 300);
    TEST_ASSERT_EQ(stats.remaining, stats.capacity - stats.used);

#if LC_STATS
    /* Detailed tracking only when LC_STATS is enabled */
    TEST_ASSERT_EQ(stats.alloc_count, (size_t)2);
    TEST_ASSERT(stats.peak_used >= 300);

    /* Reset and allocate smaller — peak should be preserved */
    size_t peak_before = stats.peak_used;
    lc_arena_reset(&arena);
    lc_arena_allocate(&arena, 50);

    lc_arena_get_stats(&arena, &stats);
    TEST_ASSERT_EQ(stats.reset_count, (size_t)1);
    TEST_ASSERT_EQ(stats.alloc_count, (size_t)3);
    TEST_ASSERT(stats.used < peak_before);
    TEST_ASSERT_EQ(stats.peak_used, peak_before);
#endif

    lc_arena_destroy(&arena);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* lc_arena_create */
    TEST_RUN(test_arena_create);

    /* lc_arena_allocate */
    TEST_RUN(test_arena_allocate_basic);

    /* Arena alignment */
    TEST_RUN(test_arena_alignment);

    /* Multiple allocations */
    TEST_RUN(test_arena_multiple_allocations);

    /* lc_arena_reset */
    TEST_RUN(test_arena_reset);

    /* lc_arena_destroy */
    TEST_RUN(test_arena_destroy);

    /* lc_allocate_pages / lc_free_pages */
    TEST_RUN(test_allocate_free_pages);

    /* Arena overflow */
    TEST_RUN(test_arena_overflow);

    /* Arena statistics */
    TEST_RUN(test_arena_stats);

    return test_main();
}
