/*
 * test_thread.c — tests for lightc threading and spinlock.
 */

#include "test.h"
#include <lightc/thread.h>
#include <lightc/heap.h>
#include <stdatomic.h>

/* ===== lc_thread_create / lc_thread_join ===== */

static _Atomic(int32_t) shared_value;

static int32_t set_shared_value(void *arg) {
    int32_t val = (int32_t)(intptr_t)arg;
    atomic_store(&shared_value, val);
    return 0;
}

static void test_thread_create_join(void) {
    lc_thread t;
    atomic_store(&shared_value, 0);

    bool ok = lc_thread_create(&t, set_shared_value, (void *)(intptr_t)42);
    TEST_ASSERT(ok);

    lc_thread_join(&t);

    TEST_ASSERT_EQ(atomic_load(&shared_value), 42);
}

/* ===== Multiple threads — atomic increment ===== */

#define NUM_THREADS     4
#define INCS_PER_THREAD 10000

static _Atomic(int32_t) atomic_counter;

static int32_t increment_counter(void *arg) {
    (void)arg;
    for (int i = 0; i < INCS_PER_THREAD; i++) {
        atomic_fetch_add(&atomic_counter, 1);
    }
    return 0;
}

static void test_multiple_threads_atomic(void) {
    lc_thread threads[NUM_THREADS];
    atomic_store(&atomic_counter, 0);

    for (int i = 0; i < NUM_THREADS; i++) {
        bool ok = lc_thread_create(&threads[i], increment_counter, NULL);
        TEST_ASSERT(ok);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        lc_thread_join(&threads[i]);
    }

    TEST_ASSERT_EQ(atomic_load(&atomic_counter), NUM_THREADS * INCS_PER_THREAD);
}

/* ===== Spinlock — multiple threads contend ===== */

static lc_spinlock lock = LC_SPINLOCK_INIT;
static int32_t guarded_counter;

static int32_t spinlock_increment(void *arg) {
    (void)arg;
    for (int i = 0; i < INCS_PER_THREAD; i++) {
        lc_spinlock_acquire(&lock);
        guarded_counter++;
        lc_spinlock_release(&lock);
    }
    return 0;
}

static void test_spinlock_contention(void) {
    lc_thread threads[NUM_THREADS];
    guarded_counter = 0;
    lock.state = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        bool ok = lc_thread_create(&threads[i], spinlock_increment, NULL);
        TEST_ASSERT(ok);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        lc_thread_join(&threads[i]);
    }

    TEST_ASSERT_EQ(guarded_counter, NUM_THREADS * INCS_PER_THREAD);
}

/* ===== lc_spinlock_try_acquire — lock held returns false ===== */

static lc_spinlock try_lock = LC_SPINLOCK_INIT;
static _Atomic(int32_t) try_result;

static int32_t try_acquire_thread(void *arg) {
    (void)arg;
    /* The main thread is holding the lock, so try_acquire should fail */
    bool got_it = lc_spinlock_try_acquire(&try_lock);
    atomic_store(&try_result, got_it ? 1 : 0);
    return 0;
}

static void test_spinlock_try_acquire(void) {
    try_lock.state = 0;
    atomic_store(&try_result, -1);

    /* Acquire the lock before spawning the thread */
    lc_spinlock_acquire(&try_lock);

    lc_thread t;
    bool ok = lc_thread_create(&t, try_acquire_thread, NULL);
    TEST_ASSERT(ok);

    lc_thread_join(&t);

    /* The child should have failed to acquire */
    TEST_ASSERT_EQ(atomic_load(&try_result), 0);

    lc_spinlock_release(&try_lock);

    /* Now try_acquire should succeed since lock is free */
    TEST_ASSERT(lc_spinlock_try_acquire(&try_lock));
    lc_spinlock_release(&try_lock);
}

/* ===== Thread heap allocation ===== */

static _Atomic(int32_t) heap_alloc_ok;

static int32_t thread_heap_alloc(void *arg) {
    (void)arg;
    void *p = lc_heap_allocate(256);
    if (p != NULL) {
        /* Write to verify it is usable memory */
        uint8_t *bytes = (uint8_t *)p;
        for (int i = 0; i < 256; i++) {
            bytes[i] = (uint8_t)i;
        }
        lc_heap_free(p);
        atomic_store(&heap_alloc_ok, 1);
    } else {
        atomic_store(&heap_alloc_ok, 0);
    }
    return 0;
}

static void test_thread_heap_allocation(void) {
    atomic_store(&heap_alloc_ok, 0);

    lc_thread t;
    bool ok = lc_thread_create(&t, thread_heap_alloc, NULL);
    TEST_ASSERT(ok);

    lc_thread_join(&t);

    TEST_ASSERT_EQ(atomic_load(&heap_alloc_ok), 1);
}

/* ===== Cross-thread heap free ===== */

#define CROSS_HEAP_COUNT 16

static void *cross_ptrs[CROSS_HEAP_COUNT];
static _Atomic(int32_t) cross_free_done;

static int32_t thread_free_memory(void *arg) {
    (void)arg;
    for (int i = 0; i < CROSS_HEAP_COUNT; i++) {
        lc_heap_free(cross_ptrs[i]);
    }
    atomic_store(&cross_free_done, 1);
    return 0;
}

static void test_cross_thread_heap_free(void) {
    atomic_store(&cross_free_done, 0);

    /* Main thread allocates */
    for (int i = 0; i < CROSS_HEAP_COUNT; i++) {
        cross_ptrs[i] = lc_heap_allocate(64);
        TEST_ASSERT_NOT_NULL(cross_ptrs[i]);
    }

    /* Child thread frees */
    lc_thread t;
    bool ok = lc_thread_create(&t, thread_free_memory, NULL);
    TEST_ASSERT(ok);

    lc_thread_join(&t);

    TEST_ASSERT_EQ(atomic_load(&cross_free_done), 1);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* thread create/join */
    TEST_RUN(test_thread_create_join);

    /* multiple threads — atomic counter */
    TEST_RUN(test_multiple_threads_atomic);

    /* spinlock contention */
    TEST_RUN(test_spinlock_contention);

    /* spinlock try_acquire */
    TEST_RUN(test_spinlock_try_acquire);

    /* cross-thread heap free */
    TEST_RUN(test_cross_thread_heap_free);

    /* thread heap allocation */
    TEST_RUN(test_thread_heap_allocation);

    return test_main();
}
