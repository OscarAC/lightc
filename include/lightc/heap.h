#ifndef LIGHTC_HEAP_H
#define LIGHTC_HEAP_H

#include "types.h"

/*
 * Heap allocator — individual alloc/free.
 *
 * Small allocations (≤ 2048): bucket allocator with free lists.
 * Large allocations (> 2048): direct mmap, munmap on free.
 *
 * Every allocation has a hidden header storing its size class,
 * so lc_heap_free knows how to return it.
 */

/* Allocate `size` bytes. Returns NULL on failure. */
[[gnu::malloc, gnu::hot, gnu::alloc_size(1)]]
void *lc_heap_allocate(size_t size);

/* Allocate `size` bytes, zero-filled. Returns NULL on failure. */
[[gnu::malloc, gnu::hot, gnu::alloc_size(1)]]
void *lc_heap_allocate_zeroed(size_t size);

/* Resize an allocation. Returns NULL on failure (original untouched). */
[[gnu::hot, gnu::alloc_size(2)]]
void *lc_heap_reallocate(void *ptr, size_t new_size);

/* Free a heap allocation. NULL is safe to pass. */
[[gnu::hot]]
void lc_heap_free(void *ptr);

/* --- Statistics ---
 *
 * Controlled by LC_STATS. Default: on in debug (no NDEBUG), off in release.
 * Override: compile with -DLC_STATS=1 to force on, -DLC_STATS=0 to force off.
 *
 * When disabled, lc_heap_get_stats() zeroes the struct and
 * lc_heap_reset_stats() is a no-op. Zero overhead on the hot path.
 */

#ifndef LC_STATS
# ifdef NDEBUG
#  define LC_STATS 0
# else
#  define LC_STATS 1
# endif
#endif

typedef struct {
    /* Counters — cumulative since process start */
    uint64_t total_allocations;       /* total calls to allocate */
    uint64_t total_frees;             /* total calls to free */
    uint64_t total_bytes_allocated;   /* sum of all requested sizes */
    uint64_t total_bytes_freed;       /* sum of all freed sizes */

    /* Current state */
    uint64_t active_allocations;      /* allocations not yet freed */
    uint64_t active_bytes;            /* bytes currently in use (requested sizes) */

    /* Peak */
    uint64_t peak_active_allocations; /* max active_allocations ever seen */
    uint64_t peak_active_bytes;       /* max active_bytes ever seen */

    /* Large allocation tracking */
    uint64_t large_allocations;       /* total large (mmap) allocations */
    uint64_t large_cache_hits;        /* large allocs served from cache */
    uint64_t large_cache_count;       /* current entries in the large cache */
} lc_heap_stats;

/* Collect current heap statistics (zeroes struct when LC_STATS=0). */
void lc_heap_get_stats(lc_heap_stats *stats);

/* Reset cumulative counters (no-op when LC_STATS=0). */
void lc_heap_reset_stats(void);

#endif /* LIGHTC_HEAP_H */
