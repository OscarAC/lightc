/*
 * bench.h — minimal self-hosted benchmark harness for lightc.
 *
 * Freestanding: uses only lightc facilities. No libc.
 *
 * Usage:
 *   #include "bench.h"
 *
 *   void bench_something(bench_state *b) {
 *       for (int64_t i = 0; i < b->iterations; i++) {
 *           // ... code to benchmark ...
 *       }
 *   }
 *
 *   int main(int argc, char **argv, char **envp) {
 *       (void)argc; (void)argv; (void)envp;
 *       BENCH_SUITE("my benchmarks");
 *       BENCH_RUN("operation name", bench_something);
 *       return 0;
 *   }
 *
 * The harness auto-calibrates iteration count so each benchmark runs
 * for approximately BENCH_TARGET_MS milliseconds, then reports:
 *   - ns/op   (nanoseconds per operation)
 *   - ops/sec (operations per second)
 */

#ifndef LIGHTC_BENCH_H
#define LIGHTC_BENCH_H

#include <lightc/print.h>
#include <lightc/string.h>
#include <lightc/time.h>
#include <lightc/types.h>

/* --- Configuration --- */

/* Target wall time per benchmark in milliseconds */
#define BENCH_TARGET_MS 500

/* Minimum iterations for calibration probe */
#define BENCH_MIN_ITERS 8

/* Output fd */
#define BENCH_FD 1

/* --- State passed to benchmark functions --- */

typedef struct {
    int64_t iterations;  /* number of iterations to run */
} bench_state;

/* Benchmark function signature */
typedef void (*bench_func)(bench_state *b);

/* --- Internal helpers --- */

static inline void _bench_print(const char *s) {
    lc_print_string(BENCH_FD, s, lc_string_length(s));
}

/*
 * Right-align a number in a field of `width` characters.
 * Prints leading spaces then the number.
 */
static inline void _bench_print_number_padded(int64_t value, int width) {
    /* Count digits */
    int64_t tmp = value < 0 ? -value : value;
    int digits = 1;
    while (tmp >= 10) { tmp /= 10; digits++; }
    if (value < 0) digits++;

    for (int i = digits; i < width; i++)
        lc_print_char(BENCH_FD, ' ');
    lc_print_signed(BENCH_FD, value);
}

/*
 * Print with thousands separators: 1234567 -> 1,234,567
 */
static inline void _bench_print_thousands(int64_t value) {
    if (value < 0) {
        lc_print_char(BENCH_FD, '-');
        value = -value;
    }
    if (value < 1000) {
        lc_print_signed(BENCH_FD, value);
        return;
    }

    /* Extract groups of 3 digits, print recursively */
    int64_t groups[7]; /* enough for 10^21 */
    int ngroups = 0;
    while (value > 0) {
        groups[ngroups++] = value % 1000;
        value /= 1000;
    }

    /* Print most significant group (no leading zeros) */
    lc_print_signed(BENCH_FD, groups[ngroups - 1]);

    /* Print remaining groups with leading zeros */
    for (int i = ngroups - 2; i >= 0; i--) {
        lc_print_char(BENCH_FD, ',');
        int64_t g = groups[i];
        if (g < 100) lc_print_char(BENCH_FD, '0');
        if (g < 10)  lc_print_char(BENCH_FD, '0');
        lc_print_signed(BENCH_FD, g);
    }
}

/*
 * Run a benchmark function with auto-calibrated iteration count.
 * Returns nanoseconds per operation.
 */
static inline int64_t _bench_run_calibrated(bench_func fn) {
    bench_state b;
    int64_t target_ns = (int64_t)BENCH_TARGET_MS * 1000000LL;

    /*
     * Calibration: start with a small iteration count, measure,
     * then scale up to hit the target time.
     */
    b.iterations = BENCH_MIN_ITERS;

    for (;;) {
        int64_t start = lc_time_start_timer();
        fn(&b);
        int64_t elapsed_ns = lc_time_elapsed_nanoseconds(start);

        /* If we ran long enough, report */
        if (elapsed_ns >= target_ns / 2 || b.iterations >= 1000000000LL) {
            return elapsed_ns / b.iterations;
        }

        /* Scale up: estimate iterations needed for target time */
        if (elapsed_ns <= 0) elapsed_ns = 1;
        int64_t next = b.iterations * target_ns / elapsed_ns;

        /* Grow by at least 2x to converge quickly, cap growth at 100x */
        if (next < b.iterations * 2) next = b.iterations * 2;
        if (next > b.iterations * 100) next = b.iterations * 100;
        if (next > 1000000000LL) next = 1000000000LL;

        b.iterations = next;
    }
}

/* --- Public macros --- */

#define BENCH_SUITE(name)                           \
    do {                                            \
        _bench_print("\n");                         \
        _bench_print(name);                         \
        _bench_print("\n");                         \
        for (size_t _i = 0; _i < lc_string_length(name); _i++) \
            lc_print_char(BENCH_FD, '-');            \
        _bench_print("\n");                         \
    } while (0)

/*
 * BENCH_RUN("label", function)
 *
 * Output format:
 *   label                    1,234 ns/op       810,372 ops/sec
 */
#define BENCH_RUN(label, fn)                                            \
    do {                                                                \
        int64_t _ns_per_op = _bench_run_calibrated(fn);                 \
        /* Print label, left-padded to 30 chars */                      \
        _bench_print("  ");                                             \
        _bench_print(label);                                            \
        size_t _label_len = lc_string_length(label);                    \
        for (size_t _p = _label_len; _p < 28; _p++)                    \
            lc_print_char(BENCH_FD, ' ');                               \
        /* ns/op */                                                     \
        _bench_print_number_padded(_ns_per_op, 8);                      \
        _bench_print(" ns/op");                                         \
        /* ops/sec */                                                   \
        int64_t _ops_sec = _ns_per_op > 0 ? 1000000000LL / _ns_per_op : 0; \
        _bench_print("    ");                                           \
        _bench_print_thousands(_ops_sec);                               \
        _bench_print(" ops/sec\n");                                     \
    } while (0)

/*
 * BENCH_RUN_N("label", function, custom_iterations)
 *
 * Run with a fixed iteration count (skip calibration).
 * Useful when calibration doesn't make sense (e.g., setup-heavy benchmarks).
 */
#define BENCH_RUN_N(label, fn, n)                                       \
    do {                                                                \
        bench_state _b;                                                 \
        _b.iterations = (n);                                            \
        int64_t _start = lc_time_start_timer();                         \
        fn(&_b);                                                        \
        int64_t _elapsed = lc_time_elapsed_nanoseconds(_start);         \
        int64_t _ns_per_op = _elapsed / _b.iterations;                  \
        _bench_print("  ");                                             \
        _bench_print(label);                                            \
        size_t _label_len = lc_string_length(label);                    \
        for (size_t _p = _label_len; _p < 28; _p++)                    \
            lc_print_char(BENCH_FD, ' ');                               \
        _bench_print_number_padded(_ns_per_op, 8);                      \
        _bench_print(" ns/op");                                         \
        int64_t _ops_sec = _ns_per_op > 0 ? 1000000000LL / _ns_per_op : 0; \
        _bench_print("    ");                                           \
        _bench_print_thousands(_ops_sec);                               \
        _bench_print(" ops/sec\n");                                     \
    } while (0)

#endif /* LIGHTC_BENCH_H */
