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

/* Create a new thread running func(arg). value = tid on success. */
[[nodiscard]] lc_result lc_thread_create(lc_thread *thread, lc_thread_func func, void *arg);

/* Create a new thread with a custom stack size. value = tid on success. */
[[nodiscard]] lc_result lc_thread_create_with_stack(lc_thread *thread, lc_thread_func func, void *arg,
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

/*
 * Mutex — futex-based, sleeps on contention.
 * Good for all critical section lengths. Prefer over spinlock unless
 * the critical section is known to be very short (<100ns).
 */

typedef struct {
    _Atomic(uint32_t) state; /* 0 = unlocked, 1 = locked, 2 = locked + waiters */
} lc_mutex;

#define LC_MUTEX_INIT { 0 }

/* Wait until the mutex is acquired. */
void lc_mutex_lock(lc_mutex *m);

/* Release the mutex. */
void lc_mutex_unlock(lc_mutex *m);

/* Try to acquire without blocking. Returns true if acquired. */
bool lc_mutex_try_lock(lc_mutex *m);

/*
 * Condition Variable — futex-based.
 * Always use with a mutex. Callers must re-check their predicate after wait.
 */

typedef struct {
    _Atomic(uint32_t) seq; /* sequence counter */
} lc_condvar;

#define LC_CONDVAR_INIT { 0 }

/* Release mutex, sleep until signaled, re-acquire mutex. */
void lc_condvar_wait(lc_condvar *cv, lc_mutex *m);

/* Wake one waiting thread. */
void lc_condvar_signal(lc_condvar *cv);

/* Wake all waiting threads. */
void lc_condvar_broadcast(lc_condvar *cv);

/*
 * Read-Write Lock — futex-based.
 * Multiple readers OR one writer.
 */

typedef struct {
    _Atomic(uint32_t) state; /* bit 31 = write-locked, bits 0-30 = reader count */
} lc_rwlock;

#define LC_RWLOCK_INIT { 0 }

void lc_rwlock_read_lock(lc_rwlock *rw);
void lc_rwlock_read_unlock(lc_rwlock *rw);
void lc_rwlock_write_lock(lc_rwlock *rw);
void lc_rwlock_write_unlock(lc_rwlock *rw);

/*
 * Barrier — N threads must arrive before any proceed.
 */

typedef struct {
    _Atomic(uint32_t) count;
    _Atomic(uint32_t) phase;
    uint32_t          threshold;
} lc_barrier;

void lc_barrier_init(lc_barrier *b, uint32_t thread_count);
void lc_barrier_wait(lc_barrier *b);

/*
 * Once — thread-safe one-time initialization.
 */

typedef struct {
    _Atomic(uint32_t) state; /* 0 = pending, 1 = running, 2 = done */
} lc_once;

#define LC_ONCE_INIT { 0 }

typedef void (*lc_once_func)(void);
void lc_call_once(lc_once *once, lc_once_func func);

/*
 * Thread-Local Storage — keyed per-thread data.
 */

#define LC_TLS_MAX_KEYS 64

typedef uint32_t lc_tls_key;

/* Allocate a new TLS key. Returns true on success. */
bool lc_tls_key_create(lc_tls_key *key);

/* Set per-thread value for a key. */
void lc_tls_set(lc_tls_key key, void *value);

/* Get per-thread value for a key. Returns NULL if unset or key invalid. */
void *lc_tls_get(lc_tls_key key);

#endif /* LIGHTC_THREAD_H */
