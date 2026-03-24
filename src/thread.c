#include <lightc/thread.h>
#include <lightc/syscall.h>

/*
 * clone flags for thread creation:
 *   CLONE_VM            — share memory space
 *   CLONE_FS            — share filesystem info
 *   CLONE_FILES         — share file descriptors
 *   CLONE_SIGHAND       — share signal handlers
 *   CLONE_THREAD        — same thread group
 *   CLONE_SYSVSEM       — share SysV semaphore undo
 *   CLONE_PARENT_SETTID — kernel stores tid at parent_tid before returning to parent
 *   CLONE_CHILD_CLEARTID — kernel clears child_tid to 0 on exit + futex wake
 */
#define CLONE_VM              0x00000100
#define CLONE_FS              0x00000200
#define CLONE_FILES           0x00000400
#define CLONE_SIGHAND         0x00000800
#define CLONE_THREAD          0x00010000
#define CLONE_SYSVSEM         0x00040000
#define CLONE_PARENT_SETTID   0x00100000
#define CLONE_CHILD_CLEARTID  0x00200000

#define CLONE_SETTLS          0x00080000

#define CLONE_THREAD_FLAGS (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | \
                            CLONE_THREAD | CLONE_SYSVSEM | \
                            CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID | \
                            CLONE_SETTLS)

/* Implemented in arch-specific assembly (arch/{x86_64,aarch64}/thread.S) */
extern int64_t lc_thread_start(uint64_t flags, uint8_t *stack_top,
                               int32_t *parent_tid, int32_t *child_tid,
                               void *tls,
                               lc_thread_func func, void *arg);

/* From heap.c — the null TLS sentinel so child threads can safely read %fs:0 */
extern void *lc_heap_tls_sentinel(void);

lc_result lc_thread_create(lc_thread *thread, lc_thread_func func, void *arg) {
    return lc_thread_create_with_stack(thread, func, arg, LC_DEFAULT_STACK_SIZE);
}

lc_result lc_thread_create_with_stack(lc_thread *thread, lc_thread_func func, void *arg,
                                      size_t stack_size) {
    size_t guard_size = 4096;  /* one page guard */
    size_t total_size = stack_size + guard_size;

    void *stack = lc_kernel_map_memory(NULL, total_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS,
                                       -1, 0);
    if (stack == MAP_FAILED) return lc_err(LC_ERR_NOMEM);

    /* Guard page at the bottom (lowest address) — stack overflow hits this */
    lc_kernel_protect_memory(stack, guard_size, PROT_NONE);

    thread->stack_base = stack;
    thread->stack_size = total_size;

    /* Stack grows downward — pass the top */
    uint8_t *stack_top = (uint8_t *)stack + total_size;

    /* Both parent_tid and child_tid point to thread->alive:
     *   CLONE_PARENT_SETTID → kernel stores tid here before returning to parent
     *   CLONE_CHILD_CLEARTID → kernel clears this to 0 on thread exit + futex wake */
    int64_t tid = lc_thread_start(CLONE_THREAD_FLAGS, stack_top,
                                  (int32_t *)&thread->alive,
                                  (int32_t *)&thread->alive,
                                  lc_heap_tls_sentinel(),
                                  func, arg);
    if (tid < 0) {
        lc_kernel_unmap_memory(stack, total_size);
        return lc_err((int32_t)(-tid));
    }

    thread->tid = (int32_t)tid;
    return lc_ok(thread->tid);
}

void lc_thread_join(lc_thread *thread) {
    /* Wait until the kernel clears alive to 0 (CLONE_CHILD_CLEARTID).
     * Use futex to sleep instead of busy-spinning. */
    int32_t tid = thread->tid;
    while (atomic_load_explicit(&thread->alive, memory_order_acquire) != 0) {
        lc_kernel_futex_wait((int32_t *)&thread->alive, tid);
    }

    /* Thread is done — free its stack */
    lc_kernel_unmap_memory(thread->stack_base, thread->stack_size);
}

/* --- Spinlock --- */

void lc_spinlock_acquire(lc_spinlock *lock) {
    while (atomic_exchange_explicit(&lock->state, 1, memory_order_acquire) != 0) {
        /* spin */
    }
}

void lc_spinlock_release(lc_spinlock *lock) {
    atomic_store_explicit(&lock->state, 0, memory_order_release);
}

bool lc_spinlock_try_acquire(lc_spinlock *lock) {
    return atomic_exchange_explicit(&lock->state, 1, memory_order_acquire) == 0;
}

/* --- Mutex (futex-based, 3-state) --- */

void lc_mutex_lock(lc_mutex *m) {
    /* Fast path: uncontended */
    uint32_t expected = 0;
    if (atomic_compare_exchange_strong_explicit(&m->state, &expected, 1,
                                                memory_order_acquire, memory_order_relaxed)) {
        return;
    }

    /* Slow path: set to contended (2) and wait */
    while (atomic_exchange_explicit(&m->state, 2, memory_order_acquire) != 0) {
        lc_kernel_futex_wait((int32_t *)&m->state, 2);
    }
}

void lc_mutex_unlock(lc_mutex *m) {
    uint32_t prev = atomic_fetch_sub_explicit(&m->state, 1, memory_order_release);
    if (prev == 2) {
        /* There were waiters — wake one */
        atomic_store_explicit(&m->state, 0, memory_order_release);
        lc_kernel_futex_wake((int32_t *)&m->state, 1);
    }
}

bool lc_mutex_try_lock(lc_mutex *m) {
    uint32_t expected = 0;
    return atomic_compare_exchange_strong_explicit(&m->state, &expected, 1,
                                                   memory_order_acquire, memory_order_relaxed);
}

/* --- Condition Variable (sequence-counter futex) --- */

void lc_condvar_wait(lc_condvar *cv, lc_mutex *m) {
    uint32_t seq = atomic_load_explicit(&cv->seq, memory_order_acquire);
    lc_mutex_unlock(m);
    lc_kernel_futex_wait((int32_t *)&cv->seq, (int32_t)seq);
    lc_mutex_lock(m);
}

void lc_condvar_signal(lc_condvar *cv) {
    atomic_fetch_add_explicit(&cv->seq, 1, memory_order_release);
    lc_kernel_futex_wake((int32_t *)&cv->seq, 1);
}

void lc_condvar_broadcast(lc_condvar *cv) {
    atomic_fetch_add_explicit(&cv->seq, 1, memory_order_release);
    lc_kernel_futex_wake((int32_t *)&cv->seq, 2147483647);
}

/* --- Read-Write Lock --- */

#define RWLOCK_WRITE_BIT 0x80000000u

void lc_rwlock_read_lock(lc_rwlock *rw) {
    for (;;) {
        uint32_t state = atomic_load_explicit(&rw->state, memory_order_acquire);
        if (state & RWLOCK_WRITE_BIT) {
            /* Writer holds lock — wait */
            lc_kernel_futex_wait((int32_t *)&rw->state, (int32_t)state);
            continue;
        }
        uint32_t expected = state;
        if (atomic_compare_exchange_weak_explicit(&rw->state, &expected, state + 1,
                                                   memory_order_acquire, memory_order_relaxed)) {
            return;
        }
    }
}

void lc_rwlock_read_unlock(lc_rwlock *rw) {
    uint32_t prev = atomic_fetch_sub_explicit(&rw->state, 1, memory_order_release);
    if (prev == 1) {
        /* Last reader — wake a waiting writer */
        lc_kernel_futex_wake((int32_t *)&rw->state, 1);
    }
}

void lc_rwlock_write_lock(lc_rwlock *rw) {
    for (;;) {
        uint32_t expected = 0;
        if (atomic_compare_exchange_weak_explicit(&rw->state, &expected, RWLOCK_WRITE_BIT,
                                                   memory_order_acquire, memory_order_relaxed)) {
            return;
        }
        lc_kernel_futex_wait((int32_t *)&rw->state, (int32_t)expected);
    }
}

void lc_rwlock_write_unlock(lc_rwlock *rw) {
    atomic_store_explicit(&rw->state, 0, memory_order_release);
    /* Wake all — readers and writers compete */
    lc_kernel_futex_wake((int32_t *)&rw->state, 2147483647);
}

/* --- Barrier --- */

void lc_barrier_init(lc_barrier *b, uint32_t thread_count) {
    atomic_store_explicit(&b->count, 0, memory_order_relaxed);
    atomic_store_explicit(&b->phase, 0, memory_order_relaxed);
    b->threshold = thread_count;
}

void lc_barrier_wait(lc_barrier *b) {
    uint32_t phase = atomic_load_explicit(&b->phase, memory_order_acquire);
    uint32_t old = atomic_fetch_add_explicit(&b->count, 1, memory_order_acq_rel);

    if (old + 1 == b->threshold) {
        /* Last thread — reset and advance phase */
        atomic_store_explicit(&b->count, 0, memory_order_relaxed);
        atomic_fetch_add_explicit(&b->phase, 1, memory_order_release);
        lc_kernel_futex_wake((int32_t *)&b->phase, 2147483647);
    } else {
        /* Wait for phase to change */
        while (atomic_load_explicit(&b->phase, memory_order_acquire) == phase) {
            lc_kernel_futex_wait((int32_t *)&b->phase, (int32_t)phase);
        }
    }
}

/* --- Once --- */

void lc_call_once(lc_once *once, lc_once_func func) {
    /* Fast path: already done */
    if (atomic_load_explicit(&once->state, memory_order_acquire) == 2) return;

    uint32_t expected = 0;
    if (atomic_compare_exchange_strong_explicit(&once->state, &expected, 1,
                                                memory_order_acquire, memory_order_relaxed)) {
        /* We won — run the function */
        func();
        atomic_store_explicit(&once->state, 2, memory_order_release);
        lc_kernel_futex_wake((int32_t *)&once->state, 2147483647);
        return;
    }

    /* Another thread is running it — wait */
    while (atomic_load_explicit(&once->state, memory_order_acquire) != 2) {
        lc_kernel_futex_wait((int32_t *)&once->state, 1);
    }
}

/* --- Thread-Local Storage --- */

static _Atomic(uint32_t) tls_next_key = 0;

/* From heap.c — returns pointer to per-thread TLS slots */
extern void **lc_heap_tls_slots(void);

bool lc_tls_key_create(lc_tls_key *key) {
    uint32_t k = atomic_fetch_add_explicit(&tls_next_key, 1, memory_order_relaxed);
    if (k >= LC_TLS_MAX_KEYS) return false;
    *key = k;
    return true;
}

void lc_tls_set(lc_tls_key key, void *value) {
    if (key >= LC_TLS_MAX_KEYS) return;
    void **slots = lc_heap_tls_slots();
    if (slots) slots[key] = value;
}

void *lc_tls_get(lc_tls_key key) {
    if (key >= LC_TLS_MAX_KEYS) return NULL;
    void **slots = lc_heap_tls_slots();
    return slots ? slots[key] : NULL;
}
