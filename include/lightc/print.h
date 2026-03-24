#ifndef LIGHTC_PRINT_H
#define LIGHTC_PRINT_H

#include "types.h"

/*
 * Print functions — composable, no format strings.
 * Every function takes a file descriptor as first argument.
 * Direct syscall per call — no buffering, no hidden state.
 */

/* Print a single character. */
void lc_print_char(int32_t fd, char c);

/* Print `length` bytes of `str`. */
void lc_print_string(int32_t fd, const char *str, size_t length);

/* Print `length` bytes of `str`, followed by a newline. */
void lc_print_line(int32_t fd, const char *str, size_t length);

/* Print a newline. */
void lc_print_newline(int32_t fd);

/* Print a signed integer in decimal. */
void lc_print_signed(int32_t fd, int64_t value);

/* Print an unsigned integer in decimal. */
void lc_print_unsigned(int32_t fd, uint64_t value);

/* Print an unsigned integer in hexadecimal with 0x prefix. */
void lc_print_hex(int32_t fd, uint64_t value);

/* Print a single byte as two hex digits (no prefix). Useful for binary/ELF dumps. */
void lc_print_byte_hex(int32_t fd, uint8_t value);

/* Print a double with default precision. */
void lc_print_double(int32_t fd, double value);

#endif /* LIGHTC_PRINT_H */
