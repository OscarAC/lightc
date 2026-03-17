#include <lightc/heap.h>
#include <lightc/memory.h>
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/thread.h>
#include <stdatomic.h>

/*
 * Block header — sits right before the pointer we return.
 */
typedef struct {
    size_t   size;
    uint32_t bucket;
    uint32_t magic;
} lc_heap_header;

#define HEAP_MAGIC      0x4C434845
#define HEAP_MAGIC_FREED 0xDEADBEEF
#define HEADER_SIZE     sizeof(lc_heap_header)

/*
 * Debug safety checks — enabled when LC_HEAP_DEBUG is defined.
 * In release builds these are compiled out for maximum performance.
 *
 * When enabled:
 *   - Double-free detection via magic poisoning
 *   - Heap corruption detection via magic validation
 *   - Use-after-free detection via 0xCC fill
 */
#ifndef LC_HEAP_DEBUG
# ifdef NDEBUG
#  define LC_HEAP_DEBUG 0
# else
#  define LC_HEAP_DEBUG 1
# endif
#endif

/*
 * Size classes — 29 buckets with ~1.25x geometric spacing.
 */
#define BUCKET_COUNT    29
#define BUCKET_LARGE    0xFFFFFFFF
#define BUCKET_NONE     0xFFFFFFFE

static const size_t bucket_block_sizes[BUCKET_COUNT] = {
                 32,  40,  48,  56,
                 64,  80,  96, 112,
                128, 160, 192, 224,
                256, 320, 384, 448,
                512, 640, 768, 896,
               1024, 1280, 1536, 1792,
               2048, 2560, 3072, 3584, 4096
};

/*
 * Large block cache — avoids mmap/munmap syscalls for repeated
 * large allocations. Freed large blocks are cached here and reused
 * on subsequent allocations of equal or smaller size.
 */
#define LARGE_CACHE_SIZE 16

typedef struct {
    void  *ptr;    /* mmap'd region (includes header) */
    size_t pages;  /* number of pages in the mapping */
} lc_large_cache_entry;

static lc_large_cache_entry large_cache[LARGE_CACHE_SIZE];
static uint32_t large_cache_count = 0;
static lc_spinlock large_cache_lock = LC_SPINLOCK_INIT;

/*
 * Remote free node — stored inside a freed block when a foreign
 * thread frees it. Forms a lock-free singly-linked list per page.
 */
typedef struct lc_free_node {
    struct lc_free_node *next;
} lc_free_node;

/*
 * Segment/Page architecture with thread-local heaps.
 *
 * Each thread owns its pages exclusively:
 *   - Owner allocates from bitmap: NO LOCK
 *   - Owner frees to bitmap: NO LOCK
 *   - Foreign thread frees: atomic CAS push to remote_free_list (NO LOCK)
 *   - Owner drains remote_free_list before allocating (one atomic exchange)
 *
 * Global lock only for: creating segments, assigning fresh pages.
 */

#define SEGMENT_SIZE        (4 * 1024 * 1024)
#define SEGMENT_ALIGN_MASK  (~(uintptr_t)(SEGMENT_SIZE - 1))
#define ALLOC_PAGE_SIZE     (64 * 1024)
#define PAGES_PER_SEGMENT   ((SEGMENT_SIZE / ALLOC_PAGE_SIZE) - 1)
#define BITMAP_MAX_WORDS    32

typedef struct lc_heap_page {
    struct lc_heap_page *next;
    uint8_t             *data_start;
    uint32_t             bucket;
    uint16_t             block_count;
    uint16_t             used_count;
    int32_t              owner_tid;
    _Atomic(lc_free_node *) remote_free_list;
    uint64_t             bitmap[BITMAP_MAX_WORDS];
} lc_heap_page;

typedef struct lc_heap_segment {
    struct lc_heap_segment *next;
    uint32_t page_count;
    uint32_t used_page_count;
    lc_heap_page pages[PAGES_PER_SEGMENT];
} lc_heap_segment;

/*
 * Thread-local heap — each thread has one.
 * Stored in a CPU register for zero-cost access:
 *   x86_64:  FS register (set via arch_prctl, read via %fs:0)
 *   aarch64: TPIDR_EL0 register (read/write from userspace)
 *
 * First alloc per thread: one gettid + one register setup (slow path).
 * All subsequent allocs/frees: single instruction to read (fast path).
 */
typedef struct lc_heap_local {
    struct lc_heap_local *self;      /* offset 0: self-pointer (x86_64 TLS read) */
    int32_t tid;
    struct lc_heap_local *next;      /* hash chain */
    lc_heap_page *pages[BUCKET_COUNT]; /* per-bucket page list */
} lc_heap_local;

/* Hash table for thread-local heaps */
#define HEAP_LOCAL_TABLE_SIZE 64
static lc_heap_local *heap_local_table[HEAP_LOCAL_TABLE_SIZE] = {0};
static lc_spinlock heap_local_table_lock = LC_SPINLOCK_INIT;

/* Global state */
static lc_heap_segment *all_segments = NULL;
static lc_spinlock heap_global_lock = LC_SPINLOCK_INIT;

/* Statistics counters — compiled out when LC_STATS=0 */
#if LC_STATS
static _Atomic(uint64_t) stat_total_allocs = 0;
static _Atomic(uint64_t) stat_total_frees = 0;
static _Atomic(uint64_t) stat_total_bytes_allocated = 0;
static _Atomic(uint64_t) stat_total_bytes_freed = 0;
static _Atomic(uint64_t) stat_active_allocs = 0;
static _Atomic(uint64_t) stat_active_bytes = 0;
static _Atomic(uint64_t) stat_peak_allocs = 0;
static _Atomic(uint64_t) stat_peak_bytes = 0;
static _Atomic(uint64_t) stat_large_allocs = 0;
static _Atomic(uint64_t) stat_large_cache_hits = 0;

static void stat_track_alloc(size_t size) {
    atomic_fetch_add_explicit(&stat_total_allocs, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&stat_total_bytes_allocated, size, memory_order_relaxed);
    uint64_t active = atomic_fetch_add_explicit(&stat_active_allocs, 1, memory_order_relaxed) + 1;
    uint64_t bytes = atomic_fetch_add_explicit(&stat_active_bytes, size, memory_order_relaxed) + size;
    /* Update peaks (relaxed compare-exchange loop) */
    uint64_t peak = atomic_load_explicit(&stat_peak_allocs, memory_order_relaxed);
    while (active > peak) {
        if (atomic_compare_exchange_weak_explicit(&stat_peak_allocs, &peak, active,
                memory_order_relaxed, memory_order_relaxed)) break;
    }
    peak = atomic_load_explicit(&stat_peak_bytes, memory_order_relaxed);
    while (bytes > peak) {
        if (atomic_compare_exchange_weak_explicit(&stat_peak_bytes, &peak, bytes,
                memory_order_relaxed, memory_order_relaxed)) break;
    }
}

static void stat_track_free(size_t size) {
    atomic_fetch_add_explicit(&stat_total_frees, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&stat_total_bytes_freed, size, memory_order_relaxed);
    atomic_fetch_sub_explicit(&stat_active_allocs, 1, memory_order_relaxed);
    atomic_fetch_sub_explicit(&stat_active_bytes, size, memory_order_relaxed);
}
#else
static inline void stat_track_alloc(size_t size) { (void)size; }
static inline void stat_track_free(size_t size)  { (void)size; }
#endif

/* --- Helpers --- */

[[gnu::const]]
static uint8_t *segment_data_start(lc_heap_segment *seg) {
    return (uint8_t *)seg + ALLOC_PAGE_SIZE;
}

static lc_heap_page *page_from_pointer(void *ptr) {
    lc_heap_segment *seg = (lc_heap_segment *)((uintptr_t)ptr & SEGMENT_ALIGN_MASK);
    size_t offset = (uint8_t *)ptr - segment_data_start(seg);
    uint32_t page_idx = offset / ALLOC_PAGE_SIZE;
    return &seg->pages[page_idx];
}

[[gnu::const, gnu::hot]]
static uint32_t find_bucket(size_t size) {
    size_t needed = size + HEADER_SIZE;
    if (needed <= 32) return 0;
    if (needed > 4096) return BUCKET_LARGE;

    uint32_t bit = 63 - (uint32_t)__builtin_clzll(needed);
    uint32_t group = bit - 5;
    uint32_t base = group * 4;

    for (uint32_t i = base; i < BUCKET_COUNT; i++) {
        if (needed <= bucket_block_sizes[i]) return i;
    }
    return BUCKET_LARGE;
}

[[gnu::const]]
static size_t bucket_usable_size(uint32_t bucket) {
    return bucket_block_sizes[bucket] - HEADER_SIZE;
}

/* --- Bitmap operations --- */

[[gnu::hot]]
static void *bitmap_allocate_block(lc_heap_page *page) {
    size_t block_size = bucket_block_sizes[page->bucket];
    uint32_t words = (page->block_count + 63) / 64;

    for (uint32_t w = 0; w < words; w++) {
        if (page->bitmap[w] != UINT64_MAX) {
            uint32_t bit = (uint32_t)__builtin_ctzll(~page->bitmap[w]);
            page->bitmap[w] |= (1ULL << bit);
            page->used_count++;
            uint32_t block_idx = w * 64 + bit;
            return page->data_start + block_idx * block_size;
        }
    }
    return NULL;
}

[[gnu::hot]]
static void bitmap_free_block(lc_heap_page *page, void *block_ptr) {
    size_t block_size = bucket_block_sizes[page->bucket];
    uint32_t block_idx = (uint32_t)((uint8_t *)block_ptr - page->data_start) / (uint32_t)block_size;
    uint32_t word_idx = block_idx / 64;
    uint32_t bit_idx = block_idx % 64;
    page->bitmap[word_idx] &= ~(1ULL << bit_idx);
    page->used_count--;
}

/* --- Remote free list (lock-free CAS push) --- */

static void remote_free_push(lc_heap_page *page, void *block_ptr) {
    lc_free_node *node = (lc_free_node *)block_ptr;
    lc_free_node *old_head;
    do {
        old_head = atomic_load_explicit(&page->remote_free_list, memory_order_relaxed);
        node->next = old_head;
    } while (!atomic_compare_exchange_weak_explicit(
        &page->remote_free_list, &old_head, node,
        memory_order_release, memory_order_relaxed));
}

/* Drain all remote frees back into the bitmap. Called by owner only. */
static void drain_remote_frees(lc_heap_page *page) {
    lc_free_node *list = atomic_exchange_explicit(
        &page->remote_free_list, NULL, memory_order_acquire);

    size_t block_size = bucket_block_sizes[page->bucket];
    while (list != NULL) {
        lc_free_node *next = list->next;
        uint32_t block_idx = (uint32_t)((uint8_t *)list - page->data_start) / (uint32_t)block_size;
        uint32_t word_idx = block_idx / 64;
        uint32_t bit_idx = block_idx % 64;
        page->bitmap[word_idx] &= ~(1ULL << bit_idx);
        page->used_count--;
        list = next;
    }
}

/* --- TLS register access (zero-cost thread-local storage) --- */

/*
 * x86_64: FS register points to lc_heap_local, %fs:0 reads self-pointer.
 * aarch64: TPIDR_EL0 holds lc_heap_local pointer directly.
 */

#define ARCH_SET_FS 0x1002

/*
 * Read/write the per-thread TLS pointer.
 *
 * x86_64: FS register. We set FS base to &local, and local->self = &local,
 *   so %fs:0 gives us the pointer. Before first setup, FS base is 0 and
 *   %fs:0 would segfault, so we set up a null sentinel at program init.
 *
 * aarch64: TPIDR_EL0 register, directly readable (0 before setup = NULL).
 */

/* Null sentinel: FS/TPIDR points here before a thread sets up TLS.
 * self = NULL tells get_local() this thread is uninitialized.
 * On x86_64, we point FS here so %fs:0 reads NULL instead of segfaulting. */
static lc_heap_local tls_null_sentinel = { .self = NULL };

static inline lc_heap_local *tls_read(void) {
#if defined(__x86_64__)
    lc_heap_local *local;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(local));
    return local;
#elif defined(__aarch64__)
    lc_heap_local *local;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(local));
    return local;
#endif
}

static inline void tls_write(lc_heap_local *local) {
#if defined(__x86_64__)
    lc_syscall2(SYS_arch_prctl, ARCH_SET_FS, (int64_t)local);
#elif defined(__aarch64__)
    __asm__ volatile("msr tpidr_el0, %0" :: "r"(local));
#endif
}

/* --- Thread-local heap lookup --- */

/*
 * Fast path: read TLS register (1 instruction, ~1 cycle).
 * Slow path: first call per thread — gettid + mmap + arch_prctl.
 */
/*
 * On x86_64, reading %fs:0 with FS base = 0 segfaults. New threads
 * start with FS = 0 (clone doesn't set it). We export lc_heap_tls_sentinel()
 * so the thread module can set FS to the null sentinel via CLONE_SETTLS.
 */
void *lc_heap_tls_sentinel(void) {
    return &tls_null_sentinel;
}

static bool main_thread_tls_ready = false;

static lc_heap_local *get_local(void) {
    /* Main thread's first call: FS is 0, reading %fs:0 would segfault.
     * Set FS to the null sentinel first. After this, tls_read() is safe
     * for the main thread. Child threads get FS set via CLONE_SETTLS. */
    if (__builtin_expect(!main_thread_tls_ready, 0)) {
        main_thread_tls_ready = true;
        tls_write(&tls_null_sentinel);
    }

    /* Fast path: read TLS register (1 instruction + 1 predicted branch).
     * Returns non-NULL if this thread has a local heap set up.
     * Returns NULL if TLS points at tls_null_sentinel (self = NULL). */
    lc_heap_local *local = tls_read();
    if (local != NULL) return local;

    /* Slow path: first alloc on this thread */
    int32_t tid = lc_kernel_get_thread_id();

    /* Check if already in hash table (e.g., thread reuse) */
    uint32_t slot = (uint32_t)tid % HEAP_LOCAL_TABLE_SIZE;
    local = heap_local_table[slot];
    while (local != NULL) {
        if (local->tid == tid) {
            tls_write(local);
            return local;
        }
        local = local->next;
    }

    /* Create new local heap via raw mmap (can't use lc_heap_allocate) */
    local = (lc_heap_local *)lc_kernel_map_memory(
        NULL, sizeof(lc_heap_local),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (local == MAP_FAILED) return NULL;

    local->self = local;  /* self-pointer for x86_64 %fs:0 read */
    local->tid = tid;
    for (uint32_t i = 0; i < BUCKET_COUNT; i++)
        local->pages[i] = NULL;

    /* Insert into hash table */
    lc_spinlock_acquire(&heap_local_table_lock);
    local->next = heap_local_table[slot];
    heap_local_table[slot] = local;
    lc_spinlock_release(&heap_local_table_lock);

    /* Set TLS register — all future reads are free */
    tls_write(local);

    return local;
}

/* --- Segment lifecycle --- */

static void segment_release(lc_heap_segment *seg) {
    lc_heap_segment **pp = &all_segments;
    while (*pp != NULL) {
        if (*pp == seg) {
            *pp = seg->next;
            break;
        }
        pp = &(*pp)->next;
    }
    lc_kernel_unmap_memory(seg, SEGMENT_SIZE);
}

[[gnu::cold]]
static void page_reclaim(lc_heap_page *page) {
    lc_kernel_advise_memory(page->data_start, ALLOC_PAGE_SIZE, MADV_DONTNEED);
    page->bucket = BUCKET_NONE;
    page->block_count = 0;
    page->used_count = 0;
    page->owner_tid = 0;
    page->next = NULL;
    atomic_store_explicit(&page->remote_free_list, NULL, memory_order_relaxed);

    lc_heap_segment *seg = (lc_heap_segment *)((uintptr_t)page->data_start & SEGMENT_ALIGN_MASK);
    seg->used_page_count--;

    if (seg->used_page_count == 0) {
        segment_release(seg);
    }
}

[[gnu::cold]]
static lc_heap_segment *segment_create(void) {
    void *raw = lc_kernel_map_memory(NULL, SEGMENT_SIZE * 2,
                                     PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) return NULL;

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = (addr + SEGMENT_SIZE - 1) & SEGMENT_ALIGN_MASK;

    if (aligned > addr)
        lc_kernel_unmap_memory((void *)addr, aligned - addr);

    uintptr_t seg_end = aligned + SEGMENT_SIZE;
    uintptr_t raw_end = addr + SEGMENT_SIZE * 2;
    if (seg_end < raw_end)
        lc_kernel_unmap_memory((void *)seg_end, raw_end - seg_end);

    lc_heap_segment *seg = (lc_heap_segment *)aligned;
    seg->next = all_segments;
    all_segments = seg;
    seg->page_count = PAGES_PER_SEGMENT;
    seg->used_page_count = 0;

    uint8_t *data_base = segment_data_start(seg);
    for (uint32_t i = 0; i < PAGES_PER_SEGMENT; i++) {
        seg->pages[i].next = NULL;
        seg->pages[i].data_start = data_base + i * ALLOC_PAGE_SIZE;
        seg->pages[i].bucket = BUCKET_NONE;
        seg->pages[i].block_count = 0;
        seg->pages[i].used_count = 0;
        seg->pages[i].owner_tid = 0;
        atomic_init(&seg->pages[i].remote_free_list, NULL);
    }

    return seg;
}

/* Initialize a page for a bucket. Does NOT add to any list. */
static lc_heap_page *page_init_for_bucket(lc_heap_segment *seg, uint32_t page_idx,
                                          uint32_t bucket, int32_t owner_tid) {
    lc_heap_page *page = &seg->pages[page_idx];
    size_t block_size = bucket_block_sizes[bucket];

    page->bucket = bucket;
    page->block_count = (uint16_t)(ALLOC_PAGE_SIZE / block_size);
    page->used_count = 0;
    page->owner_tid = owner_tid;
    atomic_store_explicit(&page->remote_free_list, NULL, memory_order_relaxed);

    uint32_t words_needed = (page->block_count + 63) / 64;
    for (uint32_t i = 0; i < words_needed; i++)
        page->bitmap[i] = 0;

    uint32_t remainder = page->block_count % 64;
    if (remainder != 0)
        page->bitmap[words_needed - 1] = ~((1ULL << remainder) - 1);

    for (uint32_t i = words_needed; i < BITMAP_MAX_WORDS; i++)
        page->bitmap[i] = UINT64_MAX;

    return page;
}

/* Get a fresh page from any segment. Caller holds heap_global_lock. */
static lc_heap_page *get_fresh_page(uint32_t bucket, int32_t owner_tid) {
    for (lc_heap_segment *seg = all_segments; seg != NULL; seg = seg->next) {
        if (seg->used_page_count < seg->page_count) {
            for (uint32_t i = 0; i < seg->page_count; i++) {
                if (seg->pages[i].bucket == BUCKET_NONE) {
                    seg->used_page_count++;
                    return page_init_for_bucket(seg, i, bucket, owner_tid);
                }
            }
        }
    }

    lc_heap_segment *seg = segment_create();
    if (seg == NULL) return NULL;

    seg->used_page_count++;
    return page_init_for_bucket(seg, 0, bucket, owner_tid);
}

/* Remove a page from a thread-local page list. */
static void remove_page_from_list(lc_heap_page **list_head, lc_heap_page *page) {
    lc_heap_page **pp = list_head;
    while (*pp != NULL) {
        if (*pp == page) {
            *pp = page->next;
            page->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* --- Public API --- */

void *lc_heap_allocate(size_t size) {
    if (size == 0) size = 1;

    uint32_t bucket = find_bucket(size);

    if (__builtin_expect(bucket == BUCKET_LARGE, 0)) {
        size_t total = size + HEADER_SIZE;
        size_t pages = (total + LC_PAGE_SIZE - 1) / LC_PAGE_SIZE;

        /* Try the large block cache first */
        uint8_t *block = NULL;
        lc_spinlock_acquire(&large_cache_lock);
        uint32_t best = UINT32_MAX;
        size_t best_pages = (size_t)-1;
        for (uint32_t i = 0; i < large_cache_count; i++) {
            if (large_cache[i].pages >= pages && large_cache[i].pages < best_pages) {
                best = i;
                best_pages = large_cache[i].pages;
                if (best_pages == pages) break; /* exact match */
            }
        }
        if (best != UINT32_MAX) {
            block = large_cache[best].ptr;
            /* Remove from cache by swapping with last */
            large_cache[best] = large_cache[--large_cache_count];
        }
        lc_spinlock_release(&large_cache_lock);

        if (block != NULL) {
#if LC_STATS
            atomic_fetch_add_explicit(&stat_large_cache_hits, 1, memory_order_relaxed);
#endif
        } else {
            block = lc_allocate_pages(pages);
            if (__builtin_expect(block == NULL, 0)) return NULL;
        }

#if LC_STATS
        atomic_fetch_add_explicit(&stat_large_allocs, 1, memory_order_relaxed);
#endif
        stat_track_alloc(size);

        lc_heap_header *header = (lc_heap_header *)block;
        header->size = size;
        header->bucket = BUCKET_LARGE;
        header->magic = HEAP_MAGIC;
        return block + HEADER_SIZE;
    }

    lc_heap_local *local = get_local();
    if (__builtin_expect(local == NULL, 0)) return NULL;

    /* Walk our page list, drain remote frees, find a page with free blocks */
    lc_heap_page *page = local->pages[bucket];
    while (page != NULL) {
        drain_remote_frees(page);
        if (page->used_count < page->block_count)
            break;
        page = page->next;
    }

    /* No page with free blocks — get a fresh one */
    if (__builtin_expect(page == NULL, 0)) {
        lc_spinlock_acquire(&heap_global_lock);
        page = get_fresh_page(bucket, local->tid);
        lc_spinlock_release(&heap_global_lock);

        if (page == NULL) return NULL;

        /* Prepend to our page list */
        page->next = local->pages[bucket];
        local->pages[bucket] = page;
    }

    /* Allocate from bitmap — no lock, we own this page */
    void *block = bitmap_allocate_block(page);
    if (block == NULL) return NULL;

    lc_heap_header *header = (lc_heap_header *)block;
    header->size = size;
    header->bucket = bucket;
    header->magic = HEAP_MAGIC;

    stat_track_alloc(size);
    return (uint8_t *)block + HEADER_SIZE;
}

void *lc_heap_allocate_zeroed(size_t size) {
    void *ptr = lc_heap_allocate(size);
    if (ptr != NULL)
        lc_bytes_fill(ptr, 0, size);
    return ptr;
}

void lc_heap_free(void *ptr) {
    if (ptr == NULL) return;

    lc_heap_header *header = (lc_heap_header *)((uint8_t *)ptr - HEADER_SIZE);
    stat_track_free(header->size);

    /* Validate magic — detect double-free and heap corruption (debug only) */
#if LC_HEAP_DEBUG
    if (header->magic == HEAP_MAGIC_FREED) {
        lc_kernel_write_bytes(2, "lightc: double-free detected\n", 29);
        lc_kernel_exit(134);
    }
    if (header->magic != HEAP_MAGIC) {
        lc_kernel_write_bytes(2, "lightc: heap corruption detected\n", 33);
        lc_kernel_exit(134);
    }
#endif

    if (header->bucket == BUCKET_LARGE) {
        size_t total = header->size + HEADER_SIZE;
        size_t pages = (total + LC_PAGE_SIZE - 1) / LC_PAGE_SIZE;
#if LC_HEAP_DEBUG
        header->magic = HEAP_MAGIC_FREED;
#endif
        /* Try to cache for reuse instead of unmapping */
        lc_spinlock_acquire(&large_cache_lock);
        if (large_cache_count < LARGE_CACHE_SIZE) {
            large_cache[large_cache_count].ptr = header;
            large_cache[large_cache_count].pages = pages;
            large_cache_count++;
            lc_spinlock_release(&large_cache_lock);
        } else {
            /* Cache full — evict the smallest entry if ours is larger */
            uint32_t smallest = 0;
            for (uint32_t i = 1; i < LARGE_CACHE_SIZE; i++) {
                if (large_cache[i].pages < large_cache[smallest].pages)
                    smallest = i;
            }
            if (pages > large_cache[smallest].pages) {
                void *evict_ptr = large_cache[smallest].ptr;
                size_t evict_pages = large_cache[smallest].pages;
                large_cache[smallest].ptr = header;
                large_cache[smallest].pages = pages;
                lc_spinlock_release(&large_cache_lock);
                lc_free_pages(evict_ptr, evict_pages);
            } else {
                lc_spinlock_release(&large_cache_lock);
                lc_free_pages(header, pages);
            }
        }
        return;
    }

    lc_heap_page *page = page_from_pointer(ptr);
    lc_heap_local *local = get_local(); /* TLS read: 1 instruction */

    if (local == NULL) {
        /* Can't determine ownership — use remote free as fallback */
#if LC_HEAP_DEBUG
        header->magic = HEAP_MAGIC_FREED;
        lc_bytes_fill((uint8_t *)header + HEADER_SIZE, 0xCC, header->size);
#endif
        remote_free_push(page, header);
        return;
    }

    if (page->owner_tid == local->tid) {
        /* Same thread — free directly to bitmap, no lock */
        bitmap_free_block(page, header);
#if LC_HEAP_DEBUG
        header->magic = HEAP_MAGIC_FREED;
        lc_bytes_fill((uint8_t *)header + HEADER_SIZE, 0xCC, header->size);
#endif
    } else {
        /* Different thread — CAS push to remote free list, no lock */
#if LC_HEAP_DEBUG
        header->magic = HEAP_MAGIC_FREED;
        lc_bytes_fill((uint8_t *)header + HEADER_SIZE, 0xCC, header->size);
#endif
        remote_free_push(page, header);
    }
}

void *lc_heap_reallocate(void *ptr, size_t new_size) {
    if (ptr == NULL) return lc_heap_allocate(new_size);
    if (new_size == 0) {
        lc_heap_free(ptr);
        return NULL;
    }

    lc_heap_header *header = (lc_heap_header *)((uint8_t *)ptr - HEADER_SIZE);
    size_t old_size = header->size;

    if (header->bucket != BUCKET_LARGE) {
        size_t usable = bucket_usable_size(header->bucket);
        if (new_size <= usable) {
#if LC_STATS
            /* Adjust active bytes for the size change */
            if (new_size > old_size) {
                atomic_fetch_add_explicit(&stat_active_bytes, new_size - old_size, memory_order_relaxed);
                atomic_fetch_add_explicit(&stat_total_bytes_allocated, new_size - old_size, memory_order_relaxed);
            } else {
                atomic_fetch_sub_explicit(&stat_active_bytes, old_size - new_size, memory_order_relaxed);
                atomic_fetch_add_explicit(&stat_total_bytes_freed, old_size - new_size, memory_order_relaxed);
            }
#endif
            header->size = new_size;
            return ptr;
        }
    }

    void *new_ptr = lc_heap_allocate(new_size);
    if (new_ptr == NULL) return NULL;

    size_t copy_size = old_size < new_size ? old_size : new_size;
    lc_bytes_copy(new_ptr, ptr, copy_size);
    lc_heap_free(ptr);

    return new_ptr;
}

/* --- Statistics --- */

void lc_heap_get_stats(lc_heap_stats *stats) {
#if LC_STATS
    stats->total_allocations     = atomic_load_explicit(&stat_total_allocs, memory_order_relaxed);
    stats->total_frees           = atomic_load_explicit(&stat_total_frees, memory_order_relaxed);
    stats->total_bytes_allocated = atomic_load_explicit(&stat_total_bytes_allocated, memory_order_relaxed);
    stats->total_bytes_freed     = atomic_load_explicit(&stat_total_bytes_freed, memory_order_relaxed);
    stats->active_allocations    = atomic_load_explicit(&stat_active_allocs, memory_order_relaxed);
    stats->active_bytes          = atomic_load_explicit(&stat_active_bytes, memory_order_relaxed);
    stats->peak_active_allocations = atomic_load_explicit(&stat_peak_allocs, memory_order_relaxed);
    stats->peak_active_bytes     = atomic_load_explicit(&stat_peak_bytes, memory_order_relaxed);
    stats->large_allocations     = atomic_load_explicit(&stat_large_allocs, memory_order_relaxed);
    stats->large_cache_hits      = atomic_load_explicit(&stat_large_cache_hits, memory_order_relaxed);

    /* Read current large cache count under lock */
    lc_spinlock_acquire(&large_cache_lock);
    stats->large_cache_count = large_cache_count;
    lc_spinlock_release(&large_cache_lock);
#else
    lc_bytes_fill(stats, 0, sizeof(*stats));
    /* Still report large cache count — it's always available */
    lc_spinlock_acquire(&large_cache_lock);
    stats->large_cache_count = large_cache_count;
    lc_spinlock_release(&large_cache_lock);
#endif
}

void lc_heap_reset_stats(void) {
#if LC_STATS
    atomic_store_explicit(&stat_total_allocs, 0, memory_order_relaxed);
    atomic_store_explicit(&stat_total_frees, 0, memory_order_relaxed);
    atomic_store_explicit(&stat_total_bytes_allocated, 0, memory_order_relaxed);
    atomic_store_explicit(&stat_total_bytes_freed, 0, memory_order_relaxed);
    atomic_store_explicit(&stat_peak_allocs,
        atomic_load_explicit(&stat_active_allocs, memory_order_relaxed),
        memory_order_relaxed);
    atomic_store_explicit(&stat_peak_bytes,
        atomic_load_explicit(&stat_active_bytes, memory_order_relaxed),
        memory_order_relaxed);
    atomic_store_explicit(&stat_large_allocs, 0, memory_order_relaxed);
    atomic_store_explicit(&stat_large_cache_hits, 0, memory_order_relaxed);
#endif
}
