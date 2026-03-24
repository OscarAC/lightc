/*
 * bench_thread.c — threading and coroutine benchmarks.
 *
 * Measures: thread create/join, spinlock, context switch, coroutine yield.
 */

#include "bench.h"
#include <lightc/thread.h>
#include <lightc/coroutine.h>
#include <stdatomic.h>

/* --- Thread create + join --- */

static int32_t noop_thread(void *arg) {
    (void)arg;
    return 0;
}

static void bench_thread_create_join(bench_state *b) {
    lc_thread t;
    for (int64_t i = 0; i < b->iterations; i++) {
        (void)lc_thread_create(&t, noop_thread, NULL);
        lc_thread_join(&t);
    }
}

/* --- Spinlock --- */

static lc_spinlock bench_lock = LC_SPINLOCK_INIT;

static void bench_spinlock_uncontended(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_spinlock_acquire(&bench_lock);
        lc_spinlock_release(&bench_lock);
    }
}

/* --- Coroutine yield (round-trip) --- */

static void yield_coroutine(void *arg) {
    int64_t *count = (int64_t *)arg;
    for (int64_t i = 0; i < *count; i++) {
        lc_coroutine_yield();
    }
}

static void bench_coroutine_yield_2(bench_state *b) {
    /* Two coroutines yielding back and forth.
     * Each yield is a context switch, so 2 * iterations switches total. */
    int64_t count = b->iterations;
    lc_scheduler sched = lc_scheduler_create_with_capacity(2);
    lc_coroutine_create(&sched, yield_coroutine, &count);
    lc_coroutine_create(&sched, yield_coroutine, &count);
    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);
}

/* --- Coroutine create + run + destroy --- */

static void trivial_coroutine(void *arg) {
    (void)arg;
}

static void bench_coroutine_lifecycle(bench_state *b) {
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_scheduler sched = lc_scheduler_create_with_capacity(1);
        lc_coroutine_create(&sched, trivial_coroutine, NULL);
        lc_scheduler_run(&sched);
        lc_scheduler_destroy(&sched);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    BENCH_SUITE("threads");
    BENCH_RUN("create+join",           bench_thread_create_join);
    BENCH_RUN("spinlock (uncontended)", bench_spinlock_uncontended);

    BENCH_SUITE("coroutines");
    BENCH_RUN("yield (2 coroutines)",   bench_coroutine_yield_2);
    BENCH_RUN("create+run+destroy",     bench_coroutine_lifecycle);

    return 0;
}
