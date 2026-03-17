/*
 * Exercise every string and bytes function.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>

static void say(const char *s) {
    lc_kernel_write_bytes(STDOUT, s, lc_string_length(s));
}

static void say_pass_fail(bool passed) {
    say(passed ? " PASS\n" : " FAIL\n");
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* --- lc_string_length --- */
    say("string_length");
    say_pass_fail(lc_string_length("hello") == 5
               && lc_string_length("") == 0
               && lc_string_length("a") == 1);

    /* --- lc_string_equal --- */
    say("string_equal");
    say_pass_fail(lc_string_equal("abc", 3, "abc", 3) == true
               && lc_string_equal("abc", 3, "abx", 3) == false
               && lc_string_equal("abc", 3, "ab", 2) == false
               && lc_string_equal("", 0, "", 0) == true);

    /* --- lc_string_compare --- */
    say("string_compare");
    say_pass_fail(lc_string_compare("abc", 3, "abc", 3) == 0
               && lc_string_compare("abc", 3, "abd", 3) < 0
               && lc_string_compare("abd", 3, "abc", 3) > 0
               && lc_string_compare("ab", 2, "abc", 3) < 0
               && lc_string_compare("abc", 3, "ab", 2) > 0);

    /* --- lc_string_starts_with --- */
    say("string_starts_with");
    say_pass_fail(lc_string_starts_with("hello world", 11, "hello", 5) == true
               && lc_string_starts_with("hello", 5, "hello world", 11) == false
               && lc_string_starts_with("hello", 5, "", 0) == true);

    /* --- lc_string_ends_with --- */
    say("string_ends_with");
    say_pass_fail(lc_string_ends_with("hello world", 11, "world", 5) == true
               && lc_string_ends_with("hello", 5, "world", 5) == false
               && lc_string_ends_with("hello", 5, "", 0) == true);

    /* --- lc_string_find_byte --- */
    say("string_find_byte");
    say_pass_fail(lc_string_find_byte("hello", 5, 'l') == 2
               && lc_string_find_byte("hello", 5, 'z') == -1
               && lc_string_find_byte("hello", 5, 'h') == 0
               && lc_string_find_byte("hello", 5, 'o') == 4);

    /* --- lc_string_find_substring --- */
    say("string_find_substring");
    say_pass_fail(lc_string_find_substring("hello world", 11, "world", 5) == 6
               && lc_string_find_substring("hello world", 11, "xyz", 3) == -1
               && lc_string_find_substring("hello", 5, "", 0) == 0
               && lc_string_find_substring("aabaa", 5, "ab", 2) == 1);

    /* --- lc_string_contains --- */
    say("string_contains");
    say_pass_fail(lc_string_contains("hello world", 11, "lo wo", 5) == true
               && lc_string_contains("hello", 5, "xyz", 3) == false);

    /* --- lc_bytes_copy --- */
    say("bytes_copy");
    char buf[16];
    lc_bytes_copy(buf, "hello", 6);
    say_pass_fail(lc_string_equal(buf, 5, "hello", 5));

    /* --- lc_bytes_move (overlapping) --- */
    say("bytes_move");
    char overlap[] = "abcdef";
    lc_bytes_move(overlap + 1, overlap, 5);  /* "aabcde" */
    say_pass_fail(lc_string_equal(overlap, 6, "aabcde", 6));

    /* --- lc_bytes_fill --- */
    say("bytes_fill");
    char filled[8];
    lc_bytes_fill(filled, 'X', 8);
    say_pass_fail(filled[0] == 'X' && filled[7] == 'X');

    /* --- lc_bytes_compare --- */
    say("bytes_compare");
    say_pass_fail(lc_bytes_compare("abc", "abc", 3) == 0
               && lc_bytes_compare("abc", "abd", 3) < 0);

    say("all passed\n");
    return 0;
}
