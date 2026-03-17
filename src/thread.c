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

bool lc_thread_create(lc_thread *thread, lc_thread_func func, void *arg) {
    return lc_thread_create_with_stack(thread, func, arg, LC_DEFAULT_STACK_SIZE);
}

bool lc_thread_create_with_stack(lc_thread *thread, lc_thread_func func, void *arg,
                                 size_t stack_size) {
    size_t guard_size = 4096;  /* one page guard */
    size_t total_size = stack_size + guard_size;

    void *stack = lc_kernel_map_memory(NULL, total_size,
                                       PROT_READ | PROT_WRITE,
                                       MAP_PRIVATE | MAP_ANONYMOUS,
                                       -1, 0);
    if (stack == MAP_FAILED) return false;

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
        return false;
    }

    thread->tid = (int32_t)tid;
    return true;
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
