#ifndef LIGHTC_THREAD_H
#define LIGHTC_THREAD_H

#include "types.h"
#include <stdatomic.h>

/*
 * Threading — built on raw clone syscall, no pthreads.
 *
 * Each thread gets its own mmap'd stack.
 * Join uses futex (kernel wakes us when the thread exits).
 */

#define LC_DEFAULT_STACK_SIZE (1024 * 1024) /* 1 MiB */

/* Thread function signature: takes one argument, returns exit code. */
typedef int32_t (*lc_thread_func)(void *arg);

typedef struct {
    int32_t          tid;         /* thread ID (set by kernel) */
    void            *stack_base;  /* mmap'd stack base */
    size_t           stack_size;  /* stack size in bytes */
    _Atomic(int32_t) alive;       /* tid while running, 0 when exited (cleared by kernel) */
} lc_thread;

/* Create a new thread running func(arg). Returns true on success. */
bool lc_thread_create(lc_thread *thread, lc_thread_func func, void *arg);

/* Create a new thread with a custom stack size. Returns true on success. */
bool lc_thread_create_with_stack(lc_thread *thread, lc_thread_func func, void *arg,
                                 size_t stack_size);

/* Wait for a thread to finish. Frees the thread's stack. */
void lc_thread_join(lc_thread *thread);

/*
 * Spinlock — simple test-and-set, no futex.
 * Good for short critical sections.
 */

typedef struct {
    _Atomic(uint32_t) state; /* 0 = unlocked, 1 = locked */
} lc_spinlock;

#define LC_SPINLOCK_INIT { 0 }

/* Wait until the lock is acquired. */
void lc_spinlock_acquire(lc_spinlock *lock);

/* Release the lock. */
void lc_spinlock_release(lc_spinlock *lock);

/* Try to acquire without blocking. Returns true if acquired. */
bool lc_spinlock_try_acquire(lc_spinlock *lock);

#endif /* LIGHTC_THREAD_H */
