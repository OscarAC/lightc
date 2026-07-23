#include <lightc/memory.h>
#include <lightc/syscall.h>

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

/* --- Raw page allocation --- */

lc_result_ptr lc_allocate_pages(size_t count) {
    /* count * LC_PAGE_SIZE must not wrap — a wrapped value would map a tiny
     * region while the caller believes it owns `count` pages. */
    size_t bytes;
    if (__builtin_mul_overflow(count, (size_t)LC_PAGE_SIZE, &bytes))
        return lc_err_ptr(LC_ERR_NOMEM);
    void *ptr = lc_kernel_map_memory(NULL, bytes,
                                     PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS,
                                     -1, 0);
    if (ptr == MAP_FAILED) return lc_err_ptr(LC_ERR_NOMEM);
    return lc_ok_ptr(ptr);
}

void lc_free_pages(void *ptr, size_t count) {
    lc_kernel_unmap_memory(ptr, count * LC_PAGE_SIZE);
}

/* --- Arena --- */

lc_arena lc_arena_create(size_t size) {
    lc_arena arena = {0};

    /* Round up to page boundary */
    size_t pages = align_up(size, LC_PAGE_SIZE) / LC_PAGE_SIZE;
    if (pages == 0) pages = 1;

    lc_result_ptr alloc = lc_allocate_pages(pages);
    if (lc_ptr_is_err(alloc)) return arena;
    void *mem = alloc.value;

    arena.base = mem;
    arena.capacity = pages * LC_PAGE_SIZE;
    arena.used = 0;

    return arena;
}

void lc_arena_destroy(lc_arena *arena) {
    if (arena->base != NULL) {
        lc_kernel_unmap_memory(arena->base, arena->capacity);
        arena->base = NULL;
        arena->capacity = 0;
        arena->used = 0;
    }
}

void *lc_arena_allocate_aligned(lc_arena *arena, size_t size, size_t alignment) {
    size_t aligned_offset = align_up(arena->used, alignment);

    /* Overflow-safe capacity check: aligned_offset + size must not wrap, or a
     * huge `size` would pass the check and hand out overlapping memory. */
    if (aligned_offset > arena->capacity || size > arena->capacity - aligned_offset)
        return NULL;

    void *ptr = arena->base + aligned_offset;
    arena->used = aligned_offset + size;
#if LC_STATS
    arena->alloc_count++;
    if (arena->used > arena->peak_used) arena->peak_used = arena->used;
#endif

    return ptr;
}
