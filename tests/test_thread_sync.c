/*
 * test_thread_sync.c — tests for lightc mutex, condvar, rwlock,
 *                       barrier, once, and TLS primitives.
 */

#include "test.h"
#include <lightc/thread.h>
#include <lightc/time.h>
#include <stdatomic.h>

/* ===== Mutex: basic lock/unlock ===== */

static void test_mutex_lock_unlock(void) {
    lc_mutex m = LC_MUTEX_INIT;

    lc_mutex_lock(&m);
    lc_mutex_unlock(&m);

    /* Lock again to verify it can be reacquired after unlock */
    lc_mutex_lock(&m);
    lc_mutex_unlock(&m);

    TEST_ASSERT(1);
}

/* ===== Mutex: try_lock ===== */

static lc_mutex try_mutex = LC_MUTEX_INIT;
static _Atomic(int32_t) try_mutex_result;

static int32_t try_mutex_thread(void *arg) {
    (void)arg;
    /* Main thread holds the lock, so try_lock should fail */
    bool got_it = lc_mutex_try_lock(&try_mutex);
    atomic_store(&try_mutex_result, got_it ? 1 : 0);
    if (got_it) {
        lc_mutex_unlock(&try_mutex);
    }
    return 0;
}

static void test_mutex_try_lock(void) {
    try_mutex = (lc_mutex)LC_MUTEX_INIT;
    atomic_store(&try_mutex_result, -1);

    /* Lock the mutex */
    lc_mutex_lock(&try_mutex);

    /* Spawn thread that tries to acquire */
    lc_thread t;
    TEST_ASSERT_OK(lc_thread_create(&t, try_mutex_thread, NULL));
    lc_thread_join(&t);

    /* Child should have failed */
    TEST_ASSERT_EQ(atomic_load(&try_mutex_result), 0);

    lc_mutex_unlock(&try_mutex);

    /* Now try_lock should succeed */
    TEST_ASSERT(lc_mutex_try_lock(&try_mutex));
    lc_mutex_unlock(&try_mutex);
}

/* ===== Mutex: contention ===== */

#define MUTEX_THREADS    4
#define MUTEX_INCS       10000

static lc_mutex contention_mutex = LC_MUTEX_INIT;
static int32_t  mutex_counter;

static int32_t mutex_increment(void *arg) {
    (void)arg;
    for (int i = 0; i < MUTEX_INCS; i++) {
        lc_mutex_lock(&contention_mutex);
        mutex_counter++;
        lc_mutex_unlock(&contention_mutex);
    }
    return 0;
}

static void test_mutex_contention(void) {
    lc_thread threads[MUTEX_THREADS];
    contention_mutex = (lc_mutex)LC_MUTEX_INIT;
    mutex_counter = 0;

    for (int i = 0; i < MUTEX_THREADS; i++) {
        TEST_ASSERT_OK(lc_thread_create(&threads[i], mutex_increment, NULL));
    }

    for (int i = 0; i < MUTEX_THREADS; i++) {
        lc_thread_join(&threads[i]);
    }

    TEST_ASSERT_EQ(mutex_counter, MUTEX_THREADS * MUTEX_INCS);
}

/* ===== Condvar: signal ===== */

static lc_mutex    cv_mutex = LC_MUTEX_INIT;
static lc_condvar  cv       = LC_CONDVAR_INIT;
static int32_t     cv_flag;
static _Atomic(int32_t) cv_consumer_saw;

static int32_t condvar_consumer(void *arg) {
    (void)arg;
    lc_mutex_lock(&cv_mutex);
    while (cv_flag == 0) {
        lc_condvar_wait(&cv, &cv_mutex);
    }
    atomic_store(&cv_consumer_saw, cv_flag);
    lc_mutex_unlock(&cv_mutex);
    return 0;
}

static void test_condvar_signal(void) {
    cv_mutex = (lc_mutex)LC_MUTEX_INIT;
    cv       = (lc_condvar)LC_CONDVAR_INIT;
    cv_flag  = 0;
    atomic_store(&cv_consumer_saw, 0);

    lc_thread t;
    TEST_ASSERT_OK(lc_thread_create(&t, condvar_consumer, NULL));

    /* Give consumer time to start waiting */
    lc_time_sleep_milliseconds(20);

    /* Set the flag and signal */
    lc_mutex_lock(&cv_mutex);
    cv_flag = 42;
    lc_condvar_signal(&cv);
    lc_mutex_unlock(&cv_mutex);

    lc_thread_join(&t);

    TEST_ASSERT_EQ(atomic_load(&cv_consumer_saw), 42);
}

/* ===== RWLock: multiple readers ===== */

#define RW_READERS 4

static lc_rwlock rw = LC_RWLOCK_INIT;
static _Atomic(int32_t) readers_inside;
static _Atomic(int32_t) max_readers_seen;

static int32_t rwlock_reader(void *arg) {
    (void)arg;
    lc_rwlock_read_lock(&rw);

    int32_t cur = atomic_fetch_add(&readers_inside, 1) + 1;

    /* Track the maximum number of concurrent readers */
    int32_t prev_max = atomic_load(&max_readers_seen);
    while (cur > prev_max) {
        if (atomic_compare_exchange_weak(&max_readers_seen, &prev_max, cur)) {
            break;
        }
    }

    /* Hold the lock briefly so other readers can overlap */
    lc_time_sleep_milliseconds(30);

    atomic_fetch_sub(&readers_inside, 1);
    lc_rwlock_read_unlock(&rw);
    return 0;
}

static void test_rwlock_multiple_readers(void) {
    rw = (lc_rwlock)LC_RWLOCK_INIT;
    atomic_store(&readers_inside, 0);
    atomic_store(&max_readers_seen, 0);

    lc_thread threads[RW_READERS];
    for (int i = 0; i < RW_READERS; i++) {
        TEST_ASSERT_OK(lc_thread_create(&threads[i], rwlock_reader, NULL));
    }

    for (int i = 0; i < RW_READERS; i++) {
        lc_thread_join(&threads[i]);
    }

    /* At least 2 readers should have been inside concurrently */
    TEST_ASSERT(atomic_load(&max_readers_seen) >= 2);
}

/* ===== RWLock: writer excludes readers ===== */

static lc_rwlock  rw_excl = LC_RWLOCK_INIT;
static _Atomic(int32_t) reader_entered;
static _Atomic(int32_t) writer_released;

static int32_t rwlock_blocked_reader(void *arg) {
    (void)arg;
    /* Try to read-lock while writer holds it */
    lc_rwlock_read_lock(&rw_excl);
    /* If we get here, the writer must have released */
    atomic_store(&reader_entered, atomic_load(&writer_released));
    lc_rwlock_read_unlock(&rw_excl);
    return 0;
}

static void test_rwlock_writer_excludes(void) {
    rw_excl = (lc_rwlock)LC_RWLOCK_INIT;
    atomic_store(&reader_entered, 0);
    atomic_store(&writer_released, 0);

    /* Writer acquires lock */
    lc_rwlock_write_lock(&rw_excl);

    /* Spawn reader — it should block */
    lc_thread t;
    TEST_ASSERT_OK(lc_thread_create(&t, rwlock_blocked_reader, NULL));

    lc_time_sleep_milliseconds(20);

    /* Reader should not have entered yet */
    TEST_ASSERT_EQ(atomic_load(&reader_entered), 0);

    /* Release writer */
    atomic_store(&writer_released, 1);
    lc_rwlock_write_unlock(&rw_excl);

    lc_thread_join(&t);

    /* Reader should have entered only after writer released */
    TEST_ASSERT_EQ(atomic_load(&reader_entered), 1);
}

/* ===== Barrier: sync ===== */

#define BARRIER_THREADS 4

static lc_barrier barrier;
static _Atomic(int32_t) barrier_counter;
static _Atomic(int32_t) barrier_post_values[BARRIER_THREADS];

static int32_t barrier_thread(void *arg) {
    int idx = (int)(intptr_t)arg;

    /* Increment counter before the barrier */
    atomic_fetch_add(&barrier_counter, 1);

    /* Wait for all threads */
    lc_barrier_wait(&barrier);

    /* After barrier, all threads should see counter == BARRIER_THREADS */
    atomic_store(&barrier_post_values[idx], atomic_load(&barrier_counter));
    return 0;
}

static void test_barrier_sync(void) {
    atomic_store(&barrier_counter, 0);
    for (int i = 0; i < BARRIER_THREADS; i++) {
        atomic_store(&barrier_post_values[i], 0);
    }
    lc_barrier_init(&barrier, BARRIER_THREADS);

    lc_thread threads[BARRIER_THREADS];
    for (int i = 0; i < BARRIER_THREADS; i++) {
        TEST_ASSERT_OK(lc_thread_create(&threads[i], barrier_thread, (void *)(intptr_t)i));
    }

    for (int i = 0; i < BARRIER_THREADS; i++) {
        lc_thread_join(&threads[i]);
    }

    /* Every thread should have seen counter == BARRIER_THREADS after the barrier */
    for (int i = 0; i < BARRIER_THREADS; i++) {
        TEST_ASSERT_EQ(atomic_load(&barrier_post_values[i]), BARRIER_THREADS);
    }
}

/* ===== Once: single execution ===== */

#define ONCE_THREADS 8

static lc_once once_flag = LC_ONCE_INIT;
static _Atomic(int32_t) once_counter;

static void once_init_func(void) {
    atomic_fetch_add(&once_counter, 1);
}

static int32_t once_thread(void *arg) {
    (void)arg;
    lc_call_once(&once_flag, once_init_func);
    return 0;
}

static void test_once_single_execution(void) {
    once_flag = (lc_once)LC_ONCE_INIT;
    atomic_store(&once_counter, 0);

    lc_thread threads[ONCE_THREADS];
    for (int i = 0; i < ONCE_THREADS; i++) {
        TEST_ASSERT_OK(lc_thread_create(&threads[i], once_thread, NULL));
    }

    for (int i = 0; i < ONCE_THREADS; i++) {
        lc_thread_join(&threads[i]);
    }

    /* Function should have been called exactly once */
    TEST_ASSERT_EQ(atomic_load(&once_counter), 1);
}

/* ===== TLS: isolation between threads ===== */

static lc_tls_key tls_key;
static _Atomic(intptr_t) child_tls_value;

static int32_t tls_child_thread(void *arg) {
    (void)arg;

    /* Set the TLS key to a different value in the child */
    lc_tls_set(tls_key, (void *)(intptr_t)0xBBBB);
    atomic_store(&child_tls_value, (intptr_t)lc_tls_get(tls_key));

    return 0;
}

static void test_tls_isolation(void) {
    atomic_store(&child_tls_value, 0);

    bool ok = lc_tls_key_create(&tls_key);
    TEST_ASSERT(ok);

    /* Main thread sets value */
    lc_tls_set(tls_key, (void *)(intptr_t)0xAAAA);

    /* Verify main thread can read it back */
    TEST_ASSERT_EQ((intptr_t)lc_tls_get(tls_key), (intptr_t)0xAAAA);

    /* Spawn child that sets the same key to a different value */
    lc_thread t;
    TEST_ASSERT_OK(lc_thread_create(&t, tls_child_thread, NULL));
    lc_thread_join(&t);

    /* Child should have seen its own value */
    TEST_ASSERT_EQ(atomic_load(&child_tls_value), (intptr_t)0xBBBB);

    /* Main thread's value should be unchanged */
    TEST_ASSERT_EQ((intptr_t)lc_tls_get(tls_key), (intptr_t)0xAAAA);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Mutex */
    TEST_RUN(test_mutex_lock_unlock);
    TEST_RUN(test_mutex_try_lock);
    TEST_RUN(test_mutex_contention);

    /* Condvar */
    TEST_RUN(test_condvar_signal);

    /* RWLock */
    TEST_RUN(test_rwlock_multiple_readers);
    TEST_RUN(test_rwlock_writer_excludes);

    /* Barrier */
    TEST_RUN(test_barrier_sync);

    /* Once */
    TEST_RUN(test_once_single_execution);

    /* TLS */
    TEST_RUN(test_tls_isolation);

    return test_main();
}
