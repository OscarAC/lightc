/*
 * test.h — minimal self-hosted test harness for lightc.
 *
 * Freestanding: uses only lightc's own facilities for output.
 * No libc, no assert.h, no stdio.h.
 *
 * Usage:
 *   #include "test.h"
 *
 *   void test_something(void) {
 *       TEST_ASSERT(1 + 1 == 2);
 *       TEST_ASSERT_EQ(42, 42);
 *       TEST_ASSERT_STR_EQ("hello", 5, "hello", 5);
 *   }
 *
 *   int main(int argc, char **argv, char **envp) {
 *       (void)argc; (void)argv; (void)envp;
 *       TEST_RUN(test_something);
 *       return test_main();
 *   }
 */

#ifndef LIGHTC_TEST_H
#define LIGHTC_TEST_H

#include <lightc/print.h>
#include <lightc/string.h>
#include <lightc/types.h>

/* --- Globals for tracking test results --- */

static int _test_total   = 0;
static int _test_passed  = 0;
static int _test_failed  = 0;
static int _test_current_failed = 0;

/* File descriptor for output (stdout = 1) */
#define TEST_FD 1

/* --- Internal helpers --- */

static inline void _test_print_text(const char *s) {
    lc_print_string(TEST_FD, s, lc_string_length(s));
}

static inline void _test_print_location(const char *file, int line) {
    _test_print_text("  ");
    _test_print_text(file);
    _test_print_text(":");
    lc_print_signed(TEST_FD, line);
    _test_print_text(": ");
}

static inline void _test_fail(const char *file, int line, const char *expr) {
    _test_print_location(file, line);
    _test_print_text("FAIL: ");
    _test_print_text(expr);
    lc_print_newline(TEST_FD);
    _test_current_failed++;
}

/* --- Assertion macros --- */

#define TEST_ASSERT(expr)                                           \
    do {                                                            \
        if (!(expr)) {                                              \
            _test_fail(__FILE__, __LINE__, #expr);                  \
        }                                                           \
    } while (0)

#define TEST_ASSERT_EQ(a, b)                                        \
    do {                                                            \
        if ((a) != (b)) {                                           \
            _test_fail(__FILE__, __LINE__, #a " == " #b);           \
        }                                                           \
    } while (0)

#define TEST_ASSERT_NEQ(a, b)                                       \
    do {                                                            \
        if ((a) == (b)) {                                           \
            _test_fail(__FILE__, __LINE__, #a " != " #b);           \
        }                                                           \
    } while (0)

#define TEST_ASSERT_NULL(ptr)                                       \
    do {                                                            \
        if ((ptr) != NULL) {                                        \
            _test_fail(__FILE__, __LINE__, #ptr " == NULL");        \
        }                                                           \
    } while (0)

#define TEST_ASSERT_NOT_NULL(ptr)                                   \
    do {                                                            \
        if ((ptr) == NULL) {                                        \
            _test_fail(__FILE__, __LINE__, #ptr " != NULL");        \
        }                                                           \
    } while (0)

#define TEST_ASSERT_STR_EQ(a, a_len, b, b_len)                     \
    do {                                                            \
        if (!lc_string_equal((a), (a_len), (b), (b_len))) {        \
            _test_fail(__FILE__, __LINE__,                          \
                       "string_equal(" #a ", " #b ")");             \
        }                                                           \
    } while (0)

/* --- Result assertion macros --- */

#define TEST_ASSERT_OK(result)                                      \
    do {                                                            \
        lc_result _r = (result);                                    \
        if (_r.error != 0) {                                        \
            _test_fail(__FILE__, __LINE__, #result " is ok");       \
        }                                                           \
    } while (0)

#define TEST_ASSERT_ERR(result)                                     \
    do {                                                            \
        lc_result _r = (result);                                    \
        if (_r.error == 0) {                                        \
            _test_fail(__FILE__, __LINE__, #result " is err");      \
        }                                                           \
    } while (0)

#define TEST_ASSERT_PTR_OK(result)                                  \
    do {                                                            \
        lc_result_ptr _r = (result);                                \
        if (_r.error != 0) {                                        \
            _test_fail(__FILE__, __LINE__, #result " is ok");       \
        }                                                           \
    } while (0)

#define TEST_ASSERT_PTR_ERR(result)                                 \
    do {                                                            \
        lc_result_ptr _r = (result);                                \
        if (_r.error == 0) {                                        \
            _test_fail(__FILE__, __LINE__, #result " is err");      \
        }                                                           \
    } while (0)

/* --- Test runner --- */

#define TEST_RUN(test_fn)                                           \
    do {                                                            \
        _test_current_failed = 0;                                   \
        _test_total++;                                              \
        _test_print_text("RUN  ");                                  \
        _test_print_text(#test_fn);                                 \
        lc_print_newline(TEST_FD);                                  \
        test_fn();                                                  \
        if (_test_current_failed == 0) {                            \
            _test_passed++;                                         \
            _test_print_text("  OK\n");                             \
        } else {                                                    \
            _test_failed++;                                         \
        }                                                           \
    } while (0)

/* --- Summary --- */

static inline int test_main(void) {
    lc_print_newline(TEST_FD);
    _test_print_text("========================================\n");
    _test_print_text("Tests run:    ");
    lc_print_signed(TEST_FD, _test_total);
    lc_print_newline(TEST_FD);
    _test_print_text("Tests passed: ");
    lc_print_signed(TEST_FD, _test_passed);
    lc_print_newline(TEST_FD);
    _test_print_text("Tests failed: ");
    lc_print_signed(TEST_FD, _test_failed);
    lc_print_newline(TEST_FD);
    _test_print_text("========================================\n");

    if (_test_failed > 0) {
        _test_print_text("RESULT: FAIL\n");
        return 1;
    }
    _test_print_text("RESULT: PASS\n");
    return 0;
}

#endif /* LIGHTC_TEST_H */
