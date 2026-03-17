#ifndef LIGHTC_TYPES_H
#define LIGHTC_TYPES_H

/*
 * Freestanding headers — provided by GCC, not libc.
 * These are correct on every arch the compiler supports.
 */
#include <stdint.h>   /* uint8_t, int32_t, uint64_t, ... */
#include <stddef.h>   /* size_t, ptrdiff_t, NULL, nullptr_t */

/* Syscall return: negative values are -errno */
typedef int64_t lc_sysret;

#endif /* LIGHTC_TYPES_H */
