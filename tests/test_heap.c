/*
 * test_heap.c — tests for lightc heap allocator.
 */

#include "test.h"
#include <lightc/heap.h>
#include <lightc/string.h>

/* ===== lc_heap_allocate — basic ===== */

static void test_heap_allocate_basic(void) {
    void *ptr = lc_heap_allocate(64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write and read back */
    uint8_t *bytes = (uint8_t *)ptr;
    for (int i = 0; i < 64; i++) {
        bytes[i] = (uint8_t)(i * 3);
    }
    for (int i = 0; i < 64; i++) {
        TEST_ASSERT_EQ(bytes[i], (uint8_t)(i * 3));
    }

    lc_heap_free(ptr);
}

/* ===== lc_heap_allocate with size 0 ===== */

static void test_heap_allocate_zero(void) {
    void *ptr = lc_heap_allocate(0);
    TEST_ASSERT_NOT_NULL(ptr);
    lc_heap_free(ptr);
}

/* ===== lc_heap_allocate_zeroed ===== */

static void test_heap_allocate_zeroed(void) {
    void *ptr = lc_heap_allocate_zeroed(128);
    TEST_ASSERT_NOT_NULL(ptr);

    uint8_t *bytes = (uint8_t *)ptr;
    for (int i = 0; i < 128; i++) {
        TEST_ASSERT_EQ(bytes[i], (uint8_t)0);
    }

    lc_heap_free(ptr);
}

/* ===== lc_heap_free — free and re-allocate ===== */

static void test_heap_free_and_realloc(void) {
    void *ptr = lc_heap_allocate(64);
    TEST_ASSERT_NOT_NULL(ptr);
    lc_heap_free(ptr);

    /* Re-allocate — should not crash */
    void *ptr2 = lc_heap_allocate(64);
    TEST_ASSERT_NOT_NULL(ptr2);
    lc_heap_free(ptr2);
}

/* ===== lc_heap_free(NULL) ===== */

static void test_heap_free_null(void) {
    /* Should not crash */
    lc_heap_free(NULL);
}

/* ===== lc_heap_reallocate — grow ===== */

static void test_heap_reallocate_grow(void) {
    void *ptr = lc_heap_allocate(16);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write data */
    uint8_t *bytes = (uint8_t *)ptr;
    for (int i = 0; i < 16; i++) {
        bytes[i] = (uint8_t)(i + 1);
    }

    /* Grow */
    void *ptr2 = lc_heap_reallocate(ptr, 256);
    TEST_ASSERT_NOT_NULL(ptr2);

    /* Original data preserved */
    uint8_t *bytes2 = (uint8_t *)ptr2;
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQ(bytes2[i], (uint8_t)(i + 1));
    }

    lc_heap_free(ptr2);
}

/* ===== lc_heap_reallocate — shrink ===== */

static void test_heap_reallocate_shrink(void) {
    void *ptr = lc_heap_allocate(256);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write data */
    uint8_t *bytes = (uint8_t *)ptr;
    for (int i = 0; i < 256; i++) {
        bytes[i] = (uint8_t)(i & 0xFF);
    }

    /* Shrink */
    void *ptr2 = lc_heap_reallocate(ptr, 32);
    TEST_ASSERT_NOT_NULL(ptr2);

    /* First 32 bytes preserved */
    uint8_t *bytes2 = (uint8_t *)ptr2;
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQ(bytes2[i], (uint8_t)(i & 0xFF));
    }

    lc_heap_free(ptr2);
}

/* ===== lc_heap_reallocate(NULL, size) — acts as allocate ===== */

static void test_heap_reallocate_null(void) {
    void *ptr = lc_heap_reallocate(NULL, 64);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write and read */
    uint8_t *bytes = (uint8_t *)ptr;
    bytes[0] = 0xAA;
    bytes[63] = 0xBB;
    TEST_ASSERT_EQ(bytes[0], (uint8_t)0xAA);
    TEST_ASSERT_EQ(bytes[63], (uint8_t)0xBB);

    lc_heap_free(ptr);
}

/* ===== lc_heap_reallocate(ptr, 0) — acts as free, returns NULL ===== */

static void test_heap_reallocate_to_zero(void) {
    void *ptr = lc_heap_allocate(64);
    TEST_ASSERT_NOT_NULL(ptr);

    void *result = lc_heap_reallocate(ptr, 0);
    TEST_ASSERT_NULL(result);
}

/* ===== Various bucket sizes ===== */

static void test_heap_bucket_sizes(void) {
    static const size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};

    for (int i = 0; i < 8; i++) {
        size_t sz = sizes[i];
        void *ptr = lc_heap_allocate(sz);
        TEST_ASSERT_NOT_NULL(ptr);

        /* Write first and last byte */
        uint8_t *bytes = (uint8_t *)ptr;
        bytes[0] = 0xDE;
        bytes[sz - 1] = 0xAD;
        TEST_ASSERT_EQ(bytes[0], (uint8_t)0xDE);
        TEST_ASSERT_EQ(bytes[sz - 1], (uint8_t)0xAD);

        lc_heap_free(ptr);
    }
}

/* ===== Large allocation (mmap path) ===== */

static void test_heap_large_allocation(void) {
    /* Allocate > 4096 to trigger the BUCKET_LARGE / mmap path */
    size_t sz = 8192;
    void *ptr = lc_heap_allocate(sz);
    TEST_ASSERT_NOT_NULL(ptr);

    /* Write and read across the allocation */
    uint8_t *bytes = (uint8_t *)ptr;
    for (size_t i = 0; i < sz; i++) {
        bytes[i] = (uint8_t)(i & 0xFF);
    }
    for (size_t i = 0; i < sz; i++) {
        TEST_ASSERT_EQ(bytes[i], (uint8_t)(i & 0xFF));
    }

    lc_heap_free(ptr);
}

/* ===== Alloc/free cycles ===== */

static void test_heap_alloc_free_cycles(void) {
    for (int i = 0; i < 100; i++) {
        void *ptr = lc_heap_allocate(64);
        TEST_ASSERT_NOT_NULL(ptr);

        /* Touch the memory */
        uint8_t *bytes = (uint8_t *)ptr;
        bytes[0] = (uint8_t)i;

        lc_heap_free(ptr);
    }
}

/* ===== Heap stress — rapid alloc/free of many small blocks ===== */

static void test_heap_stress(void) {
    /* Allocate many blocks, then free them all */
    #define STRESS_COUNT 200
    void *ptrs[STRESS_COUNT];

    for (int i = 0; i < STRESS_COUNT; i++) {
        ptrs[i] = lc_heap_allocate(16);
        TEST_ASSERT_NOT_NULL(ptrs[i]);

        /* Write a marker */
        uint8_t *bytes = (uint8_t *)ptrs[i];
        bytes[0] = (uint8_t)(i & 0xFF);
    }

    /* Verify markers */
    for (int i = 0; i < STRESS_COUNT; i++) {
        uint8_t *bytes = (uint8_t *)ptrs[i];
        TEST_ASSERT_EQ(bytes[0], (uint8_t)(i & 0xFF));
    }

    /* Free all */
    for (int i = 0; i < STRESS_COUNT; i++) {
        lc_heap_free(ptrs[i]);
    }

    /* Allocate again to verify allocator still works */
    for (int i = 0; i < STRESS_COUNT; i++) {
        ptrs[i] = lc_heap_allocate(16);
        TEST_ASSERT_NOT_NULL(ptrs[i]);
    }
    for (int i = 0; i < STRESS_COUNT; i++) {
        lc_heap_free(ptrs[i]);
    }

    #undef STRESS_COUNT
}

/* ===== Statistics ===== */

static void test_heap_stats(void) {
    lc_heap_stats after;

    /* API should work regardless of LC_STATS */
    lc_heap_reset_stats();

    void *a = lc_heap_allocate(100);
    void *b = lc_heap_allocate(200);
    void *c = lc_heap_allocate(300);

#if LC_STATS
    lc_heap_get_stats(&after);
    TEST_ASSERT(after.total_allocations >= 3);
    TEST_ASSERT(after.active_allocations >= 3);
    TEST_ASSERT(after.active_bytes >= 600);
    TEST_ASSERT(after.total_bytes_allocated >= 600);

    lc_heap_free(b);
    lc_heap_get_stats(&after);
    TEST_ASSERT(after.total_frees >= 1);
    TEST_ASSERT(after.active_bytes >= 400);

    /* Peak should reflect the 3-allocation high point */
    TEST_ASSERT(after.peak_active_allocations >= 3);
    TEST_ASSERT(after.peak_active_bytes >= 600);

    /* Large allocation tracking */
    void *big = lc_heap_allocate(16384);
    lc_heap_get_stats(&after);
    TEST_ASSERT(after.large_allocations >= 1);
    lc_heap_free(big);

    /* Cache hit on second large alloc of same size */
    void *big2 = lc_heap_allocate(16384);
    lc_heap_get_stats(&after);
    TEST_ASSERT(after.large_cache_hits >= 1);
    lc_heap_free(big2);
#else
    /* When disabled, get_stats should still work (zeroed counters) */
    lc_heap_get_stats(&after);
    TEST_ASSERT_EQ(after.total_allocations, (uint64_t)0);
    TEST_ASSERT_EQ(after.active_allocations, (uint64_t)0);
    lc_heap_free(b);
#endif

    lc_heap_free(a);
    lc_heap_free(c);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* lc_heap_allocate — basic */
    TEST_RUN(test_heap_allocate_basic);

    /* lc_heap_allocate with size 0 */
    TEST_RUN(test_heap_allocate_zero);

    /* lc_heap_allocate_zeroed */
    TEST_RUN(test_heap_allocate_zeroed);

    /* lc_heap_free — free and re-allocate */
    TEST_RUN(test_heap_free_and_realloc);

    /* lc_heap_free(NULL) */
    TEST_RUN(test_heap_free_null);

    /* lc_heap_reallocate — grow */
    TEST_RUN(test_heap_reallocate_grow);

    /* lc_heap_reallocate — shrink */
    TEST_RUN(test_heap_reallocate_shrink);

    /* lc_heap_reallocate(NULL, size) — acts as allocate */
    TEST_RUN(test_heap_reallocate_null);

    /* lc_heap_reallocate(ptr, 0) — acts as free */
    TEST_RUN(test_heap_reallocate_to_zero);

    /* Various bucket sizes */
    TEST_RUN(test_heap_bucket_sizes);

    /* Large allocation (mmap path) */
    TEST_RUN(test_heap_large_allocation);

    /* Alloc/free cycles */
    TEST_RUN(test_heap_alloc_free_cycles);

    /* Heap stress */
    TEST_RUN(test_heap_stress);

    /* Statistics */
    TEST_RUN(test_heap_stats);

    return test_main();
}
