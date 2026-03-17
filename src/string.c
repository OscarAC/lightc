#include <lightc/string.h>

size_t lc_string_length(const char *str) {
    return __builtin_strlen(str);
}

bool lc_string_equal(const char *a, size_t a_length, const char *b, size_t b_length) {
    if (a_length != b_length) return false;
    return __builtin_memcmp(a, b, a_length) == 0;
}

int32_t lc_string_compare(const char *a, size_t a_length, const char *b, size_t b_length) {
    size_t min_length = a_length < b_length ? a_length : b_length;
    int32_t result = __builtin_memcmp(a, b, min_length);
    if (result != 0) return result;

    /* Equal up to min_length — shorter string comes first */
    if (a_length < b_length) return -1;
    if (a_length > b_length) return  1;
    return 0;
}

bool lc_string_starts_with(const char *str, size_t str_length,
                           const char *prefix, size_t prefix_length) {
    if (prefix_length > str_length) return false;
    return __builtin_memcmp(str, prefix, prefix_length) == 0;
}

bool lc_string_ends_with(const char *str, size_t str_length,
                         const char *suffix, size_t suffix_length) {
    if (suffix_length > str_length) return false;
    return __builtin_memcmp(str + str_length - suffix_length, suffix, suffix_length) == 0;
}

int64_t lc_string_find_byte(const char *str, size_t length, char byte) {
    const void *p = __builtin_memchr(str, (unsigned char)byte, length);
    if (p == NULL) return -1;
    return (int64_t)((const char *)p - str);
}

int64_t lc_string_find_substring(const char *haystack, size_t haystack_length,
                                 const char *needle, size_t needle_length) {
    if (needle_length == 0) return 0;
    if (needle_length > haystack_length) return -1;

    size_t limit = haystack_length - needle_length;
    for (size_t i = 0; i <= limit; i++) {
        if (__builtin_memcmp(haystack + i, needle, needle_length) == 0) {
            return (int64_t)i;
        }
    }
    return -1;
}

bool lc_string_contains(const char *haystack, size_t haystack_length,
                        const char *needle, size_t needle_length) {
    return lc_string_find_substring(haystack, haystack_length,
                                   needle, needle_length) >= 0;
}
