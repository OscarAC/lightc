#ifndef LIGHTC_MEMORY_H
#define LIGHTC_MEMORY_H

#include "types.h"

/*
 * Arena allocator — bump allocation, free everything at once.
 *
 * Backed by mmap. No individual free. No fragmentation.
 * Default alignment is 16 bytes (matches x86_64 ABI / SSE).
 */

#define LC_PAGE_SIZE 4096
#define LC_DEFAULT_ALIGNMENT 16

/*
 * LC_STATS controls arena statistics tracking.
 * Default: on in debug (no NDEBUG), off in release.
 * Override: compile with -DLC_STATS=1 to force on, -DLC_STATS=0 to force off.
 */
#ifndef LC_STATS
# ifdef NDEBUG
#  define LC_STATS 0
# else
#  define LC_STATS 1
# endif
#endif

typedef struct {
    uint8_t *base;       /* start of the mmap'd region */
    size_t   capacity;   /* total size in bytes */
    size_t   used;       /* bytes allocated so far */
#if LC_STATS
    size_t   peak_used;  /* high-water mark of used bytes */
    size_t   alloc_count; /* total allocations since create/reset */
    size_t   reset_count; /* number of times reset was called */
#endif
} lc_arena;

/* Create an arena of at least `size` bytes (rounded up to page boundary). */
lc_arena lc_arena_create(size_t size);

/* Destroy the arena — unmaps all memory. Do not use the arena after this. */
void lc_arena_destroy(lc_arena *arena);

/* Allocate `size` bytes with custom alignment. Returns NULL if full. */
void *lc_arena_allocate_aligned(lc_arena *arena, size_t size, size_t alignment);

/* Allocate `size` bytes from the arena, 16-byte aligned. Returns NULL if full.
 * Inlined for performance — this is just a pointer bump. */
[[gnu::hot]]
static inline void *lc_arena_allocate(lc_arena *arena, size_t size) {
    size_t aligned_offset = (arena->used + (LC_DEFAULT_ALIGNMENT - 1))
                            & ~(size_t)(LC_DEFAULT_ALIGNMENT - 1);
    if (__builtin_expect(aligned_offset + size > arena->capacity, 0)) return NULL;
    void *ptr = arena->base + aligned_offset;
    arena->used = aligned_offset + size;
#if LC_STATS
    arena->alloc_count++;
    if (arena->used > arena->peak_used) arena->peak_used = arena->used;
#endif
    return ptr;
}

/* Reset the arena — all memory is "freed" but stays mapped. */
static inline void lc_arena_reset(lc_arena *arena) {
    arena->used = 0;
#if LC_STATS
    arena->reset_count++;
#endif
}

/* How many bytes are currently in use. */
[[gnu::pure]]
static inline size_t lc_arena_get_used(const lc_arena *arena) {
    return arena->used;
}

/* How many bytes remain available. */
[[gnu::pure]]
static inline size_t lc_arena_get_remaining(const lc_arena *arena) {
    return arena->capacity - arena->used;
}

/* --- Statistics --- */

typedef struct {
    size_t capacity;       /* total arena size in bytes */
    size_t used;           /* current bytes in use */
    size_t peak_used;      /* highest used ever seen (across resets) — 0 when LC_STATS=0 */
    size_t alloc_count;    /* total allocations since create — 0 when LC_STATS=0 */
    size_t reset_count;    /* number of resets — 0 when LC_STATS=0 */
    size_t remaining;      /* bytes available */
    double utilization;    /* used / capacity (0.0 - 1.0) */
} lc_arena_stats;

/* Collect arena statistics into a stats struct.
 * When LC_STATS=0, peak_used/alloc_count/reset_count are always 0. */
static inline void lc_arena_get_stats(const lc_arena *arena, lc_arena_stats *stats) {
    stats->capacity = arena->capacity;
    stats->used = arena->used;
#if LC_STATS
    stats->peak_used = arena->peak_used;
    stats->alloc_count = arena->alloc_count;
    stats->reset_count = arena->reset_count;
#else
    stats->peak_used = 0;
    stats->alloc_count = 0;
    stats->reset_count = 0;
#endif
    stats->remaining = arena->capacity - arena->used;
    stats->utilization = arena->capacity > 0
        ? (double)arena->used / (double)arena->capacity : 0.0;
}

/*
 * Raw page allocation — when you just want memory from the kernel.
 */

/* Allocate `count` pages. Returns NULL on failure. */
[[gnu::malloc]]
void *lc_allocate_pages(size_t count);

/* Free `count` pages starting at `ptr`. */
void lc_free_pages(void *ptr, size_t count);

#endif /* LIGHTC_MEMORY_H */
