/*
 * bench_string.c — string and byte operation benchmarks.
 */

#include "bench.h"
#include <lightc/string.h>

static char buf_a[4096];
static char buf_b[4096];

/* --- String length --- */

static void bench_strlen_short(bench_state *b) {
    const char *s = "hello world";
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile size_t len = lc_string_length(s);
        (void)len;
    }
}

static void bench_strlen_256(bench_state *b) {
    lc_bytes_fill(buf_a, 'x', 255);
    buf_a[255] = '\0';
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile size_t len = lc_string_length(buf_a);
        (void)len;
    }
}

/* --- Bytes copy --- */

static void bench_copy_64(bench_state *b) {
    lc_bytes_fill(buf_a, 'A', 64);
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_bytes_copy(buf_b, buf_a, 64);
    }
}

static void bench_copy_4096(bench_state *b) {
    lc_bytes_fill(buf_a, 'A', 4096);
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_bytes_copy(buf_b, buf_a, 4096);
    }
}

/* --- Bytes fill --- */

static void bench_fill_4096(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_bytes_fill(buf_a, 0, 4096);
    }
}

/* --- String compare --- */

static void bench_compare_equal_64(bench_state *b) {
    lc_bytes_fill(buf_a, 'x', 64);
    lc_bytes_fill(buf_b, 'x', 64);
    buf_a[64] = '\0';
    buf_b[64] = '\0';
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile bool eq = lc_string_equal(buf_a, 64, buf_b, 64);
        (void)eq;
    }
}

/* --- Find byte --- */

static void bench_find_byte_end(bench_state *b) {
    lc_bytes_fill(buf_a, 'a', 255);
    buf_a[254] = 'Z';
    buf_a[255] = '\0';
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile int64_t pos = lc_string_find_byte(buf_a, 255, 'Z');
        (void)pos;
    }
}

/* --- Substring search --- */

static void bench_find_substring(bench_state *b) {
    lc_bytes_fill(buf_a, 'a', 200);
    buf_a[195] = 'n';
    buf_a[196] = 'e';
    buf_a[197] = 'e';
    buf_a[198] = 'd';
    buf_a[199] = 'l';
    buf_a[200] = 'e';
    buf_a[201] = '\0';
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile int64_t pos = lc_string_find_substring(buf_a, 201, "needle", 6);
        (void)pos;
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    BENCH_SUITE("string: length");
    BENCH_RUN("short (11 chars)",  bench_strlen_short);
    BENCH_RUN("256 chars",         bench_strlen_256);

    BENCH_SUITE("bytes: copy");
    BENCH_RUN("64 bytes",  bench_copy_64);
    BENCH_RUN("4096 bytes", bench_copy_4096);

    BENCH_SUITE("bytes: fill");
    BENCH_RUN("4096 bytes", bench_fill_4096);

    BENCH_SUITE("string: compare");
    BENCH_RUN("equal 64 chars", bench_compare_equal_64);

    BENCH_SUITE("string: search");
    BENCH_RUN("find_byte (at end)",     bench_find_byte_end);
    BENCH_RUN("find_substring (200B)",  bench_find_substring);

    return 0;
}
