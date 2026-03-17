/*
 * test_string.c — tests for lightc string and byte functions.
 */

#include "test.h"
#include <lightc/string.h>

/* ===== lc_string_length ===== */

static void test_string_length_empty(void) {
    TEST_ASSERT_EQ(lc_string_length(""), 0);
}

static void test_string_length_short(void) {
    TEST_ASSERT_EQ(lc_string_length("hi"), 2);
}

static void test_string_length_longer(void) {
    TEST_ASSERT_EQ(lc_string_length("hello, lightc!"), 14);
}

/* ===== lc_string_compare ===== */

static void test_string_compare_equal(void) {
    TEST_ASSERT_EQ(lc_string_compare("abc", 3, "abc", 3), 0);
}

static void test_string_compare_less(void) {
    TEST_ASSERT(lc_string_compare("abc", 3, "abd", 3) < 0);
}

static void test_string_compare_greater(void) {
    TEST_ASSERT(lc_string_compare("abd", 3, "abc", 3) > 0);
}

static void test_string_compare_prefix_shorter(void) {
    TEST_ASSERT(lc_string_compare("ab", 2, "abc", 3) < 0);
}

static void test_string_compare_prefix_longer(void) {
    TEST_ASSERT(lc_string_compare("abc", 3, "ab", 2) > 0);
}

static void test_string_compare_empty(void) {
    TEST_ASSERT_EQ(lc_string_compare("", 0, "", 0), 0);
}

/* ===== lc_string_equal ===== */

static void test_string_equal_same(void) {
    TEST_ASSERT(lc_string_equal("hello", 5, "hello", 5));
}

static void test_string_equal_different(void) {
    TEST_ASSERT(!lc_string_equal("hello", 5, "world", 5));
}

static void test_string_equal_different_lengths(void) {
    TEST_ASSERT(!lc_string_equal("hello", 5, "hell", 4));
}

static void test_string_equal_empty(void) {
    TEST_ASSERT(lc_string_equal("", 0, "", 0));
}

/* ===== lc_bytes_copy ===== */

static void test_bytes_copy_basic(void) {
    char src[] = "hello";
    char dst[8] = {0};
    lc_bytes_copy(dst, src, 6); /* includes null terminator */
    TEST_ASSERT_STR_EQ(dst, 5, "hello", 5);
    TEST_ASSERT_EQ(dst[5], '\0');
}

/* ===== lc_bytes_fill ===== */

static void test_bytes_fill_basic(void) {
    uint8_t buf[8];
    lc_bytes_fill(buf, 0xAB, 8);
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQ(buf[i], 0xAB);
    }
}

static void test_bytes_fill_zero(void) {
    uint8_t buf[4] = {1, 2, 3, 4};
    lc_bytes_fill(buf, 0, 4);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQ(buf[i], 0);
    }
}

/* ===== lc_bytes_compare ===== */

static void test_bytes_compare_equal(void) {
    uint8_t a[] = {1, 2, 3, 4};
    uint8_t b[] = {1, 2, 3, 4};
    TEST_ASSERT_EQ(lc_bytes_compare(a, b, 4), 0);
}

static void test_bytes_compare_different(void) {
    uint8_t a[] = {1, 2, 3, 4};
    uint8_t b[] = {1, 2, 3, 5};
    TEST_ASSERT(lc_bytes_compare(a, b, 4) != 0);
}

/* ===== lc_string_find_byte ===== */

static void test_find_byte_found(void) {
    TEST_ASSERT_EQ(lc_string_find_byte("hello", 5, 'l'), 2);
}

static void test_find_byte_not_found(void) {
    TEST_ASSERT_EQ(lc_string_find_byte("hello", 5, 'z'), -1);
}

static void test_find_byte_first_char(void) {
    TEST_ASSERT_EQ(lc_string_find_byte("hello", 5, 'h'), 0);
}

static void test_find_byte_last_char(void) {
    TEST_ASSERT_EQ(lc_string_find_byte("hello", 5, 'o'), 4);
}

static void test_find_byte_empty(void) {
    TEST_ASSERT_EQ(lc_string_find_byte("", 0, 'a'), -1);
}

/* ===== lc_string_starts_with ===== */

static void test_starts_with_positive(void) {
    TEST_ASSERT(lc_string_starts_with("hello world", 11, "hello", 5));
}

static void test_starts_with_negative(void) {
    TEST_ASSERT(!lc_string_starts_with("hello world", 11, "world", 5));
}

static void test_starts_with_empty_prefix(void) {
    TEST_ASSERT(lc_string_starts_with("hello", 5, "", 0));
}

static void test_starts_with_equal(void) {
    TEST_ASSERT(lc_string_starts_with("hello", 5, "hello", 5));
}

static void test_starts_with_prefix_longer(void) {
    TEST_ASSERT(!lc_string_starts_with("hi", 2, "hello", 5));
}

/* ===== lc_string_ends_with ===== */

static void test_ends_with_positive(void) {
    TEST_ASSERT(lc_string_ends_with("hello world", 11, "world", 5));
}

static void test_ends_with_negative(void) {
    TEST_ASSERT(!lc_string_ends_with("hello world", 11, "hello", 5));
}

static void test_ends_with_empty_suffix(void) {
    TEST_ASSERT(lc_string_ends_with("hello", 5, "", 0));
}

static void test_ends_with_equal(void) {
    TEST_ASSERT(lc_string_ends_with("hello", 5, "hello", 5));
}

static void test_ends_with_suffix_longer(void) {
    TEST_ASSERT(!lc_string_ends_with("lo", 2, "hello", 5));
}

/* ===== lc_string_find_substring ===== */

static void test_find_substring_found(void) {
    TEST_ASSERT_EQ(lc_string_find_substring("hello world", 11, "world", 5), 6);
}

static void test_find_substring_not_found(void) {
    TEST_ASSERT_EQ(lc_string_find_substring("hello world", 11, "xyz", 3), -1);
}

static void test_find_substring_at_start(void) {
    TEST_ASSERT_EQ(lc_string_find_substring("hello world", 11, "hello", 5), 0);
}

static void test_find_substring_empty_needle(void) {
    TEST_ASSERT_EQ(lc_string_find_substring("hello", 5, "", 0), 0);
}

static void test_find_substring_needle_longer(void) {
    TEST_ASSERT_EQ(lc_string_find_substring("hi", 2, "hello", 5), -1);
}

/* ===== lc_string_contains ===== */

static void test_contains_found(void) {
    TEST_ASSERT(lc_string_contains("hello world", 11, "world", 5));
}

static void test_contains_not_found(void) {
    TEST_ASSERT(!lc_string_contains("hello world", 11, "xyz", 3));
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* lc_string_length */
    TEST_RUN(test_string_length_empty);
    TEST_RUN(test_string_length_short);
    TEST_RUN(test_string_length_longer);

    /* lc_string_compare */
    TEST_RUN(test_string_compare_equal);
    TEST_RUN(test_string_compare_less);
    TEST_RUN(test_string_compare_greater);
    TEST_RUN(test_string_compare_prefix_shorter);
    TEST_RUN(test_string_compare_prefix_longer);
    TEST_RUN(test_string_compare_empty);

    /* lc_string_equal */
    TEST_RUN(test_string_equal_same);
    TEST_RUN(test_string_equal_different);
    TEST_RUN(test_string_equal_different_lengths);
    TEST_RUN(test_string_equal_empty);

    /* lc_bytes_copy */
    TEST_RUN(test_bytes_copy_basic);

    /* lc_bytes_fill */
    TEST_RUN(test_bytes_fill_basic);
    TEST_RUN(test_bytes_fill_zero);

    /* lc_bytes_compare */
    TEST_RUN(test_bytes_compare_equal);
    TEST_RUN(test_bytes_compare_different);

    /* lc_string_find_byte */
    TEST_RUN(test_find_byte_found);
    TEST_RUN(test_find_byte_not_found);
    TEST_RUN(test_find_byte_first_char);
    TEST_RUN(test_find_byte_last_char);
    TEST_RUN(test_find_byte_empty);

    /* lc_string_starts_with */
    TEST_RUN(test_starts_with_positive);
    TEST_RUN(test_starts_with_negative);
    TEST_RUN(test_starts_with_empty_prefix);
    TEST_RUN(test_starts_with_equal);
    TEST_RUN(test_starts_with_prefix_longer);

    /* lc_string_ends_with */
    TEST_RUN(test_ends_with_positive);
    TEST_RUN(test_ends_with_negative);
    TEST_RUN(test_ends_with_empty_suffix);
    TEST_RUN(test_ends_with_equal);
    TEST_RUN(test_ends_with_suffix_longer);

    /* lc_string_find_substring */
    TEST_RUN(test_find_substring_found);
    TEST_RUN(test_find_substring_not_found);
    TEST_RUN(test_find_substring_at_start);
    TEST_RUN(test_find_substring_empty_needle);
    TEST_RUN(test_find_substring_needle_longer);

    /* lc_string_contains */
    TEST_RUN(test_contains_found);
    TEST_RUN(test_contains_not_found);

    return test_main();
}
