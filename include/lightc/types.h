#ifndef LIGHTC_TYPES_H
#define LIGHTC_TYPES_H

/*
 * Freestanding headers — provided by GCC, not libc.
 * These are correct on every arch the compiler supports.
 */
#include <stdint.h>   /* uint8_t, int32_t, uint64_t, ... */
#include <stddef.h>   /* size_t, ptrdiff_t, NULL, nullptr_t */

/* Version */
#define LC_VERSION_MAJOR 0
#define LC_VERSION_MINOR 1
#define LC_VERSION_PATCH 0

/* Returns version as a single integer: major * 10000 + minor * 100 + patch */
[[gnu::const]]
static inline int32_t lc_version(void) {
    return LC_VERSION_MAJOR * 10000 + LC_VERSION_MINOR * 100 + LC_VERSION_PATCH;
}

/* Syscall return: negative values are -errno */
typedef int64_t lc_sysret;

#include "result.h"

#endif /* LIGHTC_TYPES_H */
