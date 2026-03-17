#ifndef LIGHTC_COROUTINE_H
#define LIGHTC_COROUTINE_H

#include "types.h"

/*
 * Coroutines — cooperative multitasking in userspace.
 *
 * Each coroutine has its own stack (mmap'd) and saved registers.
 * Context switches are ~15 instructions, pure userspace, no syscalls.
 *
 * Usage:
 *   lc_scheduler sched = lc_scheduler_create();
 *   lc_coroutine *co1 = lc_coroutine_create(&sched, my_func, arg);
 *   lc_coroutine *co2 = lc_coroutine_create(&sched, other_func, arg);
 *   lc_scheduler_run(&sched);    // runs all coroutines until they finish
 *   lc_scheduler_destroy(&sched);
 */

#define LC_COROUTINE_STACK_SIZE (64 * 1024)  /* 64 KiB per coroutine */
#define LC_MAX_COROUTINES 256

/* Coroutine states */
typedef enum {
    LC_COROUTINE_READY,
    LC_COROUTINE_RUNNING,
    LC_COROUTINE_FINISHED
} lc_coroutine_state;

/* Saved CPU context — callee-saved registers only */
typedef struct {
#if defined(__x86_64__)
    uint64_t rbx, rbp, r12, r13, r14, r15, rsp;
#elif defined(__aarch64__)
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t x29, x30;  /* fp, lr */
    uint64_t sp;
#endif
} lc_coroutine_context;

/* Coroutine function signature */
typedef void (*lc_coroutine_func)(void *arg);

/* Forward declaration */
typedef struct lc_scheduler lc_scheduler;

/* A single coroutine */
typedef struct {
    lc_coroutine_context context;
    lc_coroutine_state   state;
    lc_coroutine_func    func;
    void                *arg;
    void                *stack_base;     /* mmap'd stack */
    size_t               stack_size;
    lc_scheduler        *scheduler;      /* back-pointer to scheduler */
} lc_coroutine;

/* Scheduler — manages a set of coroutines */
struct lc_scheduler {
    lc_coroutine *coroutines;     /* dynamically allocated coroutine array */
    uint32_t      capacity;       /* max coroutines (size of array) */
    uint32_t      count;          /* total coroutines */
    uint32_t      active_count;   /* coroutines not yet finished */
    uint32_t      current;        /* index of currently running coroutine */
    lc_coroutine_context main_context;  /* saved context of the caller (main thread) */
};

/* --- Scheduler API --- */

/* Initialize a scheduler with default capacity (LC_MAX_COROUTINES). */
lc_scheduler lc_scheduler_create(void);

/* Initialize a scheduler with a custom coroutine capacity.
 * The coroutine array is heap-allocated; destroy with lc_scheduler_destroy(). */
lc_scheduler lc_scheduler_create_with_capacity(uint32_t max_coroutines);

/* Destroy a scheduler and free all coroutine stacks. */
void lc_scheduler_destroy(lc_scheduler *sched);

/* Create a new coroutine in the scheduler. Returns pointer to it, or NULL if full. */
lc_coroutine *lc_coroutine_create(lc_scheduler *sched, lc_coroutine_func func, void *arg);

/* Run all coroutines until they all finish. */
void lc_scheduler_run(lc_scheduler *sched);

/* --- Called from within a coroutine --- */

/* Yield control to the next ready coroutine. */
void lc_coroutine_yield(void);

#endif /* LIGHTC_COROUTINE_H */
