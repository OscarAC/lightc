/*
 * test_time.c — tests for lightc time functions.
 */

#include "test.h"
#include <lightc/time.h>

/* ===== lc_time_now_monotonic ===== */

static void test_monotonic_positive(void) {
    int64_t t = lc_time_now_monotonic();
    TEST_ASSERT(t > 0);
}

static void test_monotonic_nondecreasing(void) {
    int64_t t1 = lc_time_now_monotonic();
    int64_t t2 = lc_time_now_monotonic();
    TEST_ASSERT(t2 >= t1);
}

/* ===== lc_time_sleep_milliseconds ===== */

static void test_sleep_milliseconds(void) {
    int64_t start = lc_time_start_timer();
    lc_time_sleep_milliseconds(5);
    int64_t elapsed_us = lc_time_elapsed_microseconds(start);

    /* Should have elapsed at least ~4ms (allow small tolerance) */
    TEST_ASSERT(elapsed_us >= 4000);
    /* Should not have slept for more than 500ms (generous upper bound) */
    TEST_ASSERT(elapsed_us < 500000);
}

/* ===== lc_time_now (wall clock) ===== */

static void test_time_now_reasonable(void) {
    lc_date_time dt = lc_time_now();
    /* Year should be >= 2025 */
    TEST_ASSERT(dt.year >= 2025);
    /* Month in range */
    TEST_ASSERT(dt.month >= 1);
    TEST_ASSERT(dt.month <= 12);
    /* Day in range */
    TEST_ASSERT(dt.day >= 1);
    TEST_ASSERT(dt.day <= 31);
    /* Hour in range */
    TEST_ASSERT(dt.hour >= 0);
    TEST_ASSERT(dt.hour <= 23);
    /* Minute in range */
    TEST_ASSERT(dt.minute >= 0);
    TEST_ASSERT(dt.minute <= 59);
    /* Second in range */
    TEST_ASSERT(dt.second >= 0);
    TEST_ASSERT(dt.second <= 59);
}

/* ===== lc_time_now_unix ===== */

static void test_time_unix_reasonable(void) {
    int64_t epoch = lc_time_now_unix();
    /* 2025-01-01 00:00:00 UTC = 1735689600 */
    TEST_ASSERT(epoch > 1735689600LL);
}

/* ===== lc_time_from_unix / lc_time_to_unix round-trip ===== */

static void test_time_from_to_unix_roundtrip(void) {
    int64_t epoch = lc_time_now_unix();
    lc_date_time dt = lc_time_from_unix(epoch);
    int64_t back = lc_time_to_unix(&dt);
    TEST_ASSERT_EQ(epoch, back);
}

/* ===== lc_time_elapsed_milliseconds ===== */

static void test_elapsed_milliseconds(void) {
    int64_t start = lc_time_start_timer();
    lc_time_sleep_milliseconds(10);
    int64_t ms = lc_time_elapsed_milliseconds(start);
    TEST_ASSERT(ms >= 5);
    TEST_ASSERT(ms < 500);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Monotonic clock */
    TEST_RUN(test_monotonic_positive);
    TEST_RUN(test_monotonic_nondecreasing);

    /* Sleep */
    TEST_RUN(test_sleep_milliseconds);

    /* Wall clock */
    TEST_RUN(test_time_now_reasonable);
    TEST_RUN(test_time_unix_reasonable);

    /* Round-trip */
    TEST_RUN(test_time_from_to_unix_roundtrip);

    /* Elapsed */
    TEST_RUN(test_elapsed_milliseconds);

    return test_main();
}
