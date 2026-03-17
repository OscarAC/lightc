/*
 * test_coroutine.c — tests for lightc coroutine scheduler.
 */

#include "test.h"
#include <lightc/coroutine.h>

/* ===== Scheduler lifecycle ===== */

static void test_scheduler_create_destroy(void) {
    lc_scheduler sched = lc_scheduler_create();
    TEST_ASSERT_EQ(sched.count, 0);
    TEST_ASSERT_EQ(sched.active_count, 0);
    lc_scheduler_destroy(&sched);
    TEST_ASSERT_EQ(sched.count, 0);
}

/* ===== Single coroutine ===== */

static int32_t single_ran;

static void single_coroutine_func(void *arg) {
    (void)arg;
    single_ran = 1;
}

static void test_single_coroutine(void) {
    single_ran = 0;
    lc_scheduler sched = lc_scheduler_create();

    lc_coroutine *co = lc_coroutine_create(&sched, single_coroutine_func, NULL);
    TEST_ASSERT_NOT_NULL(co);

    lc_scheduler_run(&sched);

    TEST_ASSERT_EQ(single_ran, 1);
    lc_scheduler_destroy(&sched);
}

/* ===== Multiple coroutines — each increments counter ===== */

static int32_t multi_counter;

static void multi_increment_func(void *arg) {
    (void)arg;
    multi_counter++;
}

static void test_multiple_coroutines(void) {
    multi_counter = 0;
    lc_scheduler sched = lc_scheduler_create();

    for (int i = 0; i < 3; i++) {
        lc_coroutine *co = lc_coroutine_create(&sched, multi_increment_func, NULL);
        TEST_ASSERT_NOT_NULL(co);
    }

    lc_scheduler_run(&sched);

    TEST_ASSERT_EQ(multi_counter, 3);
    lc_scheduler_destroy(&sched);
}

/* ===== Yield — interleaved ordering ===== */

#define ORDER_SIZE 6
static int32_t order_buf[ORDER_SIZE];
static int32_t order_idx;

static void yield_writer_a(void *arg) {
    (void)arg;
    order_buf[order_idx++] = 1;  /* A writes 1 */
    lc_coroutine_yield();
    order_buf[order_idx++] = 1;  /* A writes 1 */
    lc_coroutine_yield();
    order_buf[order_idx++] = 1;  /* A writes 1 */
}

static void yield_writer_b(void *arg) {
    (void)arg;
    order_buf[order_idx++] = 2;  /* B writes 2 */
    lc_coroutine_yield();
    order_buf[order_idx++] = 2;  /* B writes 2 */
    lc_coroutine_yield();
    order_buf[order_idx++] = 2;  /* B writes 2 */
}

static void test_coroutine_yield_interleaved(void) {
    order_idx = 0;
    for (int i = 0; i < ORDER_SIZE; i++) order_buf[i] = 0;

    lc_scheduler sched = lc_scheduler_create();

    lc_coroutine *co_a = lc_coroutine_create(&sched, yield_writer_a, NULL);
    lc_coroutine *co_b = lc_coroutine_create(&sched, yield_writer_b, NULL);
    TEST_ASSERT_NOT_NULL(co_a);
    TEST_ASSERT_NOT_NULL(co_b);

    lc_scheduler_run(&sched);

    TEST_ASSERT_EQ(order_idx, ORDER_SIZE);

    /* Expect interleaved: A(1), B(2), A(1), B(2), A(1), B(2) */
    TEST_ASSERT_EQ(order_buf[0], 1);
    TEST_ASSERT_EQ(order_buf[1], 2);
    TEST_ASSERT_EQ(order_buf[2], 1);
    TEST_ASSERT_EQ(order_buf[3], 2);
    TEST_ASSERT_EQ(order_buf[4], 1);
    TEST_ASSERT_EQ(order_buf[5], 2);

    lc_scheduler_destroy(&sched);
}

/* ===== Coroutine with argument ===== */

static int32_t received_arg;

static void arg_receiver_func(void *arg) {
    received_arg = (int32_t)(intptr_t)arg;
}

static void test_coroutine_with_argument(void) {
    received_arg = 0;
    lc_scheduler sched = lc_scheduler_create();

    lc_coroutine *co = lc_coroutine_create(&sched, arg_receiver_func, (void *)(intptr_t)99);
    TEST_ASSERT_NOT_NULL(co);

    lc_scheduler_run(&sched);

    TEST_ASSERT_EQ(received_arg, 99);
    lc_scheduler_destroy(&sched);
}

/* ===== Max coroutines — create LC_MAX_COROUTINES ===== */

static int32_t max_run_count;

static void max_coroutine_func(void *arg) {
    (void)arg;
    max_run_count++;
}

static void test_max_coroutines(void) {
    max_run_count = 0;
    lc_scheduler sched = lc_scheduler_create();

    for (int i = 0; i < LC_MAX_COROUTINES; i++) {
        lc_coroutine *co = lc_coroutine_create(&sched, max_coroutine_func, NULL);
        TEST_ASSERT_NOT_NULL(co);
    }

    lc_scheduler_run(&sched);

    TEST_ASSERT_EQ(max_run_count, LC_MAX_COROUTINES);
    lc_scheduler_destroy(&sched);
}

/* ===== Over-capacity — 257th returns NULL ===== */

static void dummy_func(void *arg) { (void)arg; }

static void test_over_capacity(void) {
    lc_scheduler sched = lc_scheduler_create();

    for (int i = 0; i < LC_MAX_COROUTINES; i++) {
        lc_coroutine *co = lc_coroutine_create(&sched, dummy_func, NULL);
        TEST_ASSERT_NOT_NULL(co);
    }

    /* The 257th should fail */
    lc_coroutine *overflow = lc_coroutine_create(&sched, dummy_func, NULL);
    TEST_ASSERT_NULL(overflow);

    /* Run and destroy to clean up the 256 stacks */
    lc_scheduler_run(&sched);
    lc_scheduler_destroy(&sched);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* scheduler lifecycle */
    TEST_RUN(test_scheduler_create_destroy);

    /* single coroutine */
    TEST_RUN(test_single_coroutine);

    /* multiple coroutines */
    TEST_RUN(test_multiple_coroutines);

    /* yield interleaving */
    TEST_RUN(test_coroutine_yield_interleaved);

    /* coroutine with argument */
    TEST_RUN(test_coroutine_with_argument);

    /* max coroutines */
    TEST_RUN(test_max_coroutines);

    /* over capacity */
    TEST_RUN(test_over_capacity);

    return test_main();
}
