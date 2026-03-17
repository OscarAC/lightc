/*
 * test_format.c — tests for lightc format builder.
 */

#include "test.h"
#include <lightc/format.h>
#include <lightc/string.h>

/* ===== lc_format_start / lc_format_finish ===== */

static void test_format_lifecycle(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 0);
    TEST_ASSERT_EQ(buf[0], '\0');
    TEST_ASSERT(!lc_format_has_overflow(&fmt));
}

/* ===== lc_format_add_string ===== */

static void test_format_add_string(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string(&fmt, "hello", 5);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 5);
    TEST_ASSERT_STR_EQ(buf, 5, "hello", 5);
}

static void test_format_add_text(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_text(&fmt, "world");
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 5);
    TEST_ASSERT_STR_EQ(buf, 5, "world", 5);
}

static void test_format_add_multiple_strings(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string(&fmt, "hello", 5);
    lc_format_add_string(&fmt, " ", 1);
    lc_format_add_string(&fmt, "world", 5);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 11);
    TEST_ASSERT_STR_EQ(buf, 11, "hello world", 11);
}

/* ===== lc_format_add_char ===== */

static void test_format_add_char(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_char(&fmt, 'A');
    lc_format_add_char(&fmt, 'B');
    lc_format_add_char(&fmt, 'C');
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 3);
    TEST_ASSERT_STR_EQ(buf, 3, "ABC", 3);
}

/* ===== lc_format_add_signed ===== */

static void test_format_signed_positive(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_signed(&fmt, 42);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 2);
    TEST_ASSERT_STR_EQ(buf, 2, "42", 2);
}

static void test_format_signed_negative(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_signed(&fmt, -123);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 4);
    TEST_ASSERT_STR_EQ(buf, 4, "-123", 4);
}

static void test_format_signed_zero(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_signed(&fmt, 0);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 1);
    TEST_ASSERT_STR_EQ(buf, 1, "0", 1);
}

static void test_format_signed_int64_min(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    /* INT64_MIN = -9223372036854775808 */
    lc_format_add_signed(&fmt, (int64_t)(-9223372036854775807LL - 1));
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 20);
    TEST_ASSERT_STR_EQ(buf, 20, "-9223372036854775808", 20);
}

/* ===== lc_format_add_unsigned ===== */

static void test_format_unsigned_zero(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_unsigned(&fmt, 0);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 1);
    TEST_ASSERT_STR_EQ(buf, 1, "0", 1);
}

static void test_format_unsigned_max(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    /* UINT64_MAX = 18446744073709551615 */
    lc_format_add_unsigned(&fmt, 18446744073709551615ULL);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 20);
    TEST_ASSERT_STR_EQ(buf, 20, "18446744073709551615", 20);
}

static void test_format_unsigned_small(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_unsigned(&fmt, 255);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 3);
    TEST_ASSERT_STR_EQ(buf, 3, "255", 3);
}

/* ===== lc_format_add_hex ===== */

static void test_format_hex_zero(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_hex(&fmt, 0);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "0x0", 3);
}

static void test_format_hex_value(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_hex(&fmt, 0xDEAD);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "0xdead", 6);
}

static void test_format_hex_255(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_hex(&fmt, 255);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "0xff", 4);
}

/* ===== lc_format_add_bool ===== */

static void test_format_bool_true(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_bool(&fmt, true);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "true", 4);
}

static void test_format_bool_false(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_bool(&fmt, false);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "false", 5);
}

/* ===== lc_format_add_binary ===== */

static void test_format_binary_zero(void) {
    char buf[128];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_binary(&fmt, 0);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "0b0", 3);
}

static void test_format_binary_value(void) {
    char buf[128];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_binary(&fmt, 0b1010);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "0b1010", 6);
}

static void test_format_binary_255(void) {
    char buf[128];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_binary(&fmt, 255);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_STR_EQ(buf, len, "0b11111111", 10);
}

/* ===== Overflow handling ===== */

static void test_format_overflow_small_buffer(void) {
    char buf[4]; /* capacity=4, usable=3 (reserve 1 for null) */
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string(&fmt, "hello", 5);
    size_t len = lc_format_finish(&fmt);
    /* Would have written 5 chars, but only 3 fit */
    TEST_ASSERT_EQ(len, 5);
    TEST_ASSERT(lc_format_has_overflow(&fmt));
    /* Buffer should be null-terminated at capacity */
    TEST_ASSERT_EQ(buf[3], '\0');
    /* First 3 chars should be correct */
    TEST_ASSERT_STR_EQ(buf, 3, "hel", 3);
}

static void test_format_overflow_exact_fit(void) {
    char buf[6]; /* capacity=6, usable=5 */
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string(&fmt, "hello", 5);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 5);
    TEST_ASSERT(!lc_format_has_overflow(&fmt));
    TEST_ASSERT_STR_EQ(buf, 5, "hello", 5);
    TEST_ASSERT_EQ(buf[5], '\0');
}

static void test_format_overflow_one_byte_buffer(void) {
    char buf[1]; /* capacity=1, usable=0 */
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_char(&fmt, 'X');
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 1);
    TEST_ASSERT(lc_format_has_overflow(&fmt));
    TEST_ASSERT_EQ(buf[0], '\0');
}

/* ===== lc_format_add_newline ===== */

static void test_format_newline(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_text(&fmt, "line1");
    lc_format_add_newline(&fmt);
    lc_format_add_text(&fmt, "line2");
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 11);
    TEST_ASSERT_STR_EQ(buf, 11, "line1\nline2", 11);
}

/* ===== lc_format_add_repeat ===== */

static void test_format_repeat(void) {
    char buf[64];
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_repeat(&fmt, '=', 5);
    size_t len = lc_format_finish(&fmt);
    TEST_ASSERT_EQ(len, 5);
    TEST_ASSERT_STR_EQ(buf, 5, "=====", 5);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* lifecycle */
    TEST_RUN(test_format_lifecycle);

    /* add_string / add_text */
    TEST_RUN(test_format_add_string);
    TEST_RUN(test_format_add_text);
    TEST_RUN(test_format_add_multiple_strings);

    /* add_char */
    TEST_RUN(test_format_add_char);

    /* add_signed */
    TEST_RUN(test_format_signed_positive);
    TEST_RUN(test_format_signed_negative);
    TEST_RUN(test_format_signed_zero);
    TEST_RUN(test_format_signed_int64_min);

    /* add_unsigned */
    TEST_RUN(test_format_unsigned_zero);
    TEST_RUN(test_format_unsigned_max);
    TEST_RUN(test_format_unsigned_small);

    /* add_hex */
    TEST_RUN(test_format_hex_zero);
    TEST_RUN(test_format_hex_value);
    TEST_RUN(test_format_hex_255);

    /* add_bool */
    TEST_RUN(test_format_bool_true);
    TEST_RUN(test_format_bool_false);

    /* add_binary */
    TEST_RUN(test_format_binary_zero);
    TEST_RUN(test_format_binary_value);
    TEST_RUN(test_format_binary_255);

    /* overflow */
    TEST_RUN(test_format_overflow_small_buffer);
    TEST_RUN(test_format_overflow_exact_fit);
    TEST_RUN(test_format_overflow_one_byte_buffer);

    /* newline / repeat */
    TEST_RUN(test_format_newline);
    TEST_RUN(test_format_repeat);

    return test_main();
}
