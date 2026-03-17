#ifndef LIGHTC_STRING_H
#define LIGHTC_STRING_H

#include "types.h"

/*
 * String functions — operate on text (null-terminated or with known length).
 * No hidden strlen calls. Caller provides length where possible.
 */

/* Count bytes until null terminator. The only function that scans. */
[[gnu::pure, gnu::nonnull(1)]]
size_t lc_string_length(const char *str);

/* Are these two strings identical? Both must be `length` bytes. */
[[gnu::pure, gnu::nonnull(1, 3)]]
bool lc_string_equal(const char *a, size_t a_length, const char *b, size_t b_length);

/* Lexicographic comparison. Returns <0, 0, or >0. */
[[gnu::pure, gnu::nonnull(1, 3)]]
int32_t lc_string_compare(const char *a, size_t a_length, const char *b, size_t b_length);

/* Does `str` start with `prefix`? */
[[gnu::pure, gnu::nonnull(1, 3)]]
bool lc_string_starts_with(const char *str, size_t str_length,
                           const char *prefix, size_t prefix_length);

/* Does `str` end with `suffix`? */
[[gnu::pure, gnu::nonnull(1, 3)]]
bool lc_string_ends_with(const char *str, size_t str_length,
                         const char *suffix, size_t suffix_length);

/* Find first occurrence of `byte` in `str`. Returns index, or -1 if not found. */
[[gnu::pure, gnu::nonnull(1)]]
int64_t lc_string_find_byte(const char *str, size_t length, char byte);

/* Find first occurrence of `needle` in `haystack`. Returns index, or -1 if not found. */
[[gnu::pure, gnu::nonnull(1, 3)]]
int64_t lc_string_find_substring(const char *haystack, size_t haystack_length,
                                 const char *needle, size_t needle_length);

/* Is `needle` present in `haystack`? */
[[gnu::pure, gnu::nonnull(1, 3)]]
bool lc_string_contains(const char *haystack, size_t haystack_length,
                        const char *needle, size_t needle_length);

/*
 * Byte functions — operate on raw memory.
 * Use compiler builtins so GCC can substitute optimized versions.
 */

/* Copy `count` bytes from `src` to `dst`. Must not overlap. */
static inline void *lc_bytes_copy(void *dst, const void *src, size_t count) {
    return __builtin_memcpy(dst, src, count);
}

/* Copy `count` bytes from `src` to `dst`. Handles overlap. */
static inline void *lc_bytes_move(void *dst, const void *src, size_t count) {
    return __builtin_memmove(dst, src, count);
}

/* Fill `count` bytes of `dst` with `value`. */
static inline void *lc_bytes_fill(void *dst, uint8_t value, size_t count) {
    return __builtin_memset(dst, value, count);
}

/* Compare `count` raw bytes. Returns <0, 0, or >0. */
static inline int32_t lc_bytes_compare(const void *a, const void *b, size_t count) {
    return __builtin_memcmp(a, b, count);
}

#endif /* LIGHTC_STRING_H */
