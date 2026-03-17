/*
 * GCC requires these symbols in freestanding mode.
 * At -O2 and above, the compiler often inlines its own versions.
 * At -O0 (debug) or when the size is not known at compile time,
 * it emits calls to these, so they must be efficient.
 *
 * The word-at-a-time technique uses the classic null-byte detection:
 *   ((x - 0x0101...) & ~x & 0x8080...) is non-zero iff x has a zero byte.
 */
#include <lightc/types.h>

#define ONES  ((size_t)-1 / 255)       /* 0x0101010101010101 */
#define HIGHS (ONES * 128)             /* 0x8080808080808080 */
#define HAS_ZERO(x) (((x) - ONES) & ~(x) & HIGHS)

void *memcpy(void *dst, const void *src, size_t count) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < count; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t count) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        for (size_t i = 0; i < count; i++) d[i] = s[i];
    } else {
        for (size_t i = count; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *dst, int value, size_t count) {
    uint8_t *d = dst;
    for (size_t i = 0; i < count; i++) d[i] = (uint8_t)value;
    return dst;
}

int memcmp(const void *a, const void *b, size_t count) {
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    for (size_t i = 0; i < count; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

size_t strlen(const char *str) {
    /* Byte-at-a-time until aligned */
    const char *s = str;
    while ((uintptr_t)s & (sizeof(size_t) - 1)) {
        if (*s == 0) return (size_t)(s - str);
        s++;
    }
    /* Word-at-a-time scan */
    const size_t *w = (const size_t *)s;
    while (!HAS_ZERO(*w)) w++;
    /* Find the zero byte within the word */
    s = (const char *)w;
    while (*s) s++;
    return (size_t)(s - str);
}

void *memchr(const void *src, int c, size_t count) {
    const uint8_t *s = src;
    uint8_t val = (uint8_t)c;

    /* Byte-at-a-time until aligned */
    while (count && ((uintptr_t)s & (sizeof(size_t) - 1))) {
        if (*s == val) return (void *)s;
        s++; count--;
    }
    /* Word-at-a-time: XOR with broadcast byte, then check for zero */
    if (count >= sizeof(size_t)) {
        size_t broadcast = ONES * val;
        const size_t *w = (const size_t *)s;
        while (count >= sizeof(size_t)) {
            if (HAS_ZERO(*w ^ broadcast)) break;
            w++; count -= sizeof(size_t);
        }
        s = (const uint8_t *)w;
    }
    /* Finish byte-at-a-time */
    while (count) {
        if (*s == val) return (void *)s;
        s++; count--;
    }
    return NULL;
}
