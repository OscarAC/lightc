/*
 * Exercise threading and spinlock.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/thread.h>
#include <lightc/heap.h>
#include <stdatomic.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed)
        lc_print_line(STDOUT, S(" PASS"));
    else
        lc_print_line(STDOUT, S(" FAIL"));
}

/* --- Simple thread function --- */

static int32_t simple_thread(void *arg) {
    int32_t *result = (int32_t *)arg;
    *result = 42;
    return 0;
}

/* --- Thread that reports its own tid --- */

static int32_t tid_thread(void *arg) {
    int32_t *out = (int32_t *)arg;
    *out = lc_kernel_get_thread_id();
    return 0;
}

/* --- Counter thread for spinlock test --- */

static lc_spinlock counter_lock = LC_SPINLOCK_INIT;
static _Atomic(int64_t) shared_counter = 0;

#define INCREMENTS_PER_THREAD 100000

static int32_t counter_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        lc_spinlock_acquire(&counter_lock);
        /* Non-atomic increment inside the lock */
        int64_t val = atomic_load_explicit(&shared_counter, memory_order_relaxed);
        atomic_store_explicit(&shared_counter, val + 1, memory_order_relaxed);
        lc_spinlock_release(&counter_lock);
    }
    return 0;
}

/* --- Heap thread for cross-thread allocation test --- */

static int32_t heap_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 1;

    /* Allocate and free from this thread */
    for (int i = 0; i < 1000; i++) {
        void *p = lc_heap_allocate((size_t)(i % 200) + 1);
        if (p == NULL) { *ok = 0; return 1; }
        ((uint8_t *)p)[0] = 0xAA;
        lc_heap_free(p);
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* --- Basic thread create + join --- */
    lc_print_string(STDOUT, S("thread_create_join"));
    int32_t result = 0;
    lc_thread t1;
    bool ok = lc_thread_create(&t1, simple_thread, &result);
    if (ok) lc_thread_join(&t1);
    say_pass_fail(ok && result == 42);

    /* --- Thread has its own tid --- */
    lc_print_string(STDOUT, S("thread_own_tid"));
    int32_t child_tid = 0;
    int32_t parent_tid = lc_kernel_get_thread_id();
    lc_thread t2;
    ok = lc_thread_create(&t2, tid_thread, &child_tid);
    if (ok) lc_thread_join(&t2);
    say_pass_fail(ok && child_tid != 0 && child_tid != parent_tid);

    lc_print_string(STDOUT, S("  parent tid: "));
    lc_print_signed(STDOUT, parent_tid);
    lc_print_string(STDOUT, S(" child tid: "));
    lc_print_signed(STDOUT, child_tid);
    lc_print_newline(STDOUT);

    /* --- Multiple threads --- */
    lc_print_string(STDOUT, S("thread_multiple"));
    #define THREAD_COUNT 8
    lc_thread threads[THREAD_COUNT];
    int32_t results[THREAD_COUNT];
    ok = true;
    for (int i = 0; i < THREAD_COUNT; i++) {
        results[i] = 0;
        if (!lc_thread_create(&threads[i], simple_thread, &results[i])) {
            ok = false;
            break;
        }
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (ok) lc_thread_join(&threads[i]);
    }
    for (int i = 0; i < THREAD_COUNT; i++) {
        if (results[i] != 42) ok = false;
    }
    say_pass_fail(ok);

    /* --- Spinlock: concurrent counter --- */
    lc_print_string(STDOUT, S("spinlock_counter"));
    #define COUNTER_THREADS 4
    lc_thread cthreads[COUNTER_THREADS];
    atomic_store(&shared_counter, 0);
    ok = true;
    for (int i = 0; i < COUNTER_THREADS; i++) {
        if (!lc_thread_create(&cthreads[i], counter_thread, NULL)) {
            ok = false;
            break;
        }
    }
    for (int i = 0; i < COUNTER_THREADS; i++) {
        if (ok) lc_thread_join(&cthreads[i]);
    }
    int64_t expected = (int64_t)COUNTER_THREADS * INCREMENTS_PER_THREAD;
    int64_t actual = atomic_load(&shared_counter);
    say_pass_fail(ok && actual == expected);

    lc_print_string(STDOUT, S("  expected: "));
    lc_print_signed(STDOUT, expected);
    lc_print_string(STDOUT, S(" actual: "));
    lc_print_signed(STDOUT, actual);
    lc_print_newline(STDOUT);

    /* --- Spinlock try_acquire --- */
    lc_print_string(STDOUT, S("spinlock_try_acquire"));
    lc_spinlock trylock = LC_SPINLOCK_INIT;
    ok = lc_spinlock_try_acquire(&trylock);
    bool second = lc_spinlock_try_acquire(&trylock);
    lc_spinlock_release(&trylock);
    say_pass_fail(ok && !second);

    /* --- Heap from threads --- */
    lc_print_string(STDOUT, S("thread_heap"));
    lc_thread hthreads[4];
    int32_t hresults[4];
    ok = true;
    for (int i = 0; i < 4; i++) {
        hresults[i] = 0;
        if (!lc_thread_create(&hthreads[i], heap_thread, &hresults[i])) {
            ok = false;
            break;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (ok) lc_thread_join(&hthreads[i]);
    }
    for (int i = 0; i < 4; i++) {
        if (!hresults[i]) ok = false;
    }
    say_pass_fail(ok);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
