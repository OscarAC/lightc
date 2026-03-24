#ifndef LIGHTC_FORMAT_H
#define LIGHTC_FORMAT_H

#include "types.h"

/*
 * Formatted text builder — type-safe alternative to printf.
 *
 * No format strings. No varargs. No mismatch bugs.
 * Just build formatted text into a buffer, then use it.
 *
 *   char buf[256];
 *   lc_format fmt = lc_format_start(buf, sizeof(buf));
 *   lc_format_add_text(&fmt, "request #");
 *   lc_format_add_unsigned(&fmt, 42);
 *   lc_format_add_text(&fmt, " took ");
 *   lc_format_add_unsigned_padded(&fmt, 7, 3, ' ');
 *   lc_format_add_text(&fmt, "ms\n");
 *   size_t len = lc_format_finish(&fmt);
 *   // buf = "request #42 took   7ms\n"
 */

typedef struct {
    char   *buffer;
    size_t  capacity;
    size_t  used;
    bool    overflow;
} lc_format;

/* --- Start / Finish --- */

/* Begin formatting into a buffer. */
[[gnu::const]]
lc_format lc_format_start(char *buffer, size_t capacity);

/* Null-terminate and return the total length written.
 * If overflow occurred, returns what WOULD have been written (like snprintf). */
size_t lc_format_finish(lc_format *fmt);

/* Did we run out of buffer space? */
[[gnu::pure]]
bool lc_format_has_overflow(const lc_format *fmt);

/* --- Add content --- */

/* Add a single character. */
[[gnu::hot]]
void lc_format_add_char(lc_format *fmt, char c);

/* Add a string with known length. */
[[gnu::hot, gnu::nonnull(1)]]
void lc_format_add_string(lc_format *fmt, const char *str, size_t length);

/* Add a null-terminated string (scans for length). */
[[gnu::nonnull(1)]]
void lc_format_add_text(lc_format *fmt, const char *str);

/* Add a newline. */
void lc_format_add_newline(lc_format *fmt);

/* Add N repetitions of a character. */
void lc_format_add_repeat(lc_format *fmt, char c, size_t count);

/* --- Numbers --- */

/* Add a signed decimal integer. */
[[gnu::hot]]
void lc_format_add_signed(lc_format *fmt, int64_t value);

/* Add an unsigned decimal integer. */
[[gnu::hot]]
void lc_format_add_unsigned(lc_format *fmt, uint64_t value);

/* Add an unsigned hex integer with 0x prefix. */
void lc_format_add_hex(lc_format *fmt, uint64_t value);

/* Add an unsigned hex integer with no prefix, lowercase. */
void lc_format_add_hex_raw(lc_format *fmt, uint64_t value);

/* Add an unsigned binary integer with 0b prefix. */
void lc_format_add_binary(lc_format *fmt, uint64_t value);

/* Add a single byte as two hex digits. */
void lc_format_add_byte_hex(lc_format *fmt, uint8_t value);

/* Add a pointer value (0x...). */
void lc_format_add_pointer(lc_format *fmt, const void *ptr);

/* Add a boolean as "true" or "false". */
void lc_format_add_bool(lc_format *fmt, bool value);

/* --- Padded / Aligned numbers --- */

/* Add unsigned integer, right-aligned in a field of `width` characters. */
void lc_format_add_unsigned_padded(lc_format *fmt, uint64_t value,
                                   uint32_t width, char pad);

/* Add signed integer, right-aligned in a field of `width` characters. */
void lc_format_add_signed_padded(lc_format *fmt, int64_t value,
                                 uint32_t width, char pad);

/* Add hex (no prefix), zero-padded to `width` digits. Useful for addresses. */
void lc_format_add_hex_padded(lc_format *fmt, uint64_t value, uint32_t width);

/* --- Padded / Aligned strings --- */

/* Add string left-aligned in a field of `width`, padded with `pad`. */
void lc_format_add_string_left(lc_format *fmt, const char *str, size_t length,
                               uint32_t width, char pad);

/* Add string right-aligned in a field of `width`, padded with `pad`. */
void lc_format_add_string_right(lc_format *fmt, const char *str, size_t length,
                                uint32_t width, char pad);

/* --- Floating-point --- */

/* Add a double with default precision (6 decimal places). */
void lc_format_add_double(lc_format *fmt, double value);

/* Add a double with custom precision (decimal places). */
void lc_format_add_double_precision(lc_format *fmt, double value, uint32_t precision);

#endif /* LIGHTC_FORMAT_H */
