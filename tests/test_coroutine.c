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

/* ===== H6 regression: aarch64 d8-d15 preserved across a context switch =====
 *
 * AAPCS64 makes the low 64 bits of v8-v15 callee-saved. Two coroutines each pin
 * a distinct bit pattern into d8-d15, yield (letting the other overwrite those
 * physical registers), then read them back. Because the values are placed by
 * inline asm the compiler doesn't track them as live, so it won't preserve them
 * around the yield itself — only lc_coroutine_switch can. If the switch dropped
 * d8-d15, each coroutine would read the OTHER's pattern.
 *
 * x86_64 SysV has no callee-saved SIMD registers, so this concern is aarch64
 * only; elsewhere the test is a no-op that trivially passes.
 */
#if defined(__aarch64__)
static void fp_load8(const double *v) {  /* v[0..7] -> d8..d15 */
    __asm__ volatile(
        "ldr d8,  [%0, #0]\n\t"  "ldr d9,  [%0, #8]\n\t"
        "ldr d10, [%0, #16]\n\t" "ldr d11, [%0, #24]\n\t"
        "ldr d12, [%0, #32]\n\t" "ldr d13, [%0, #40]\n\t"
        "ldr d14, [%0, #48]\n\t" "ldr d15, [%0, #56]\n\t"
        : : "r"(v)
        : "d8","d9","d10","d11","d12","d13","d14","d15");
}
static void fp_store8(double *v) {  /* d8..d15 -> v[0..7] */
    __asm__ volatile(
        "str d8,  [%0, #0]\n\t"  "str d9,  [%0, #8]\n\t"
        "str d10, [%0, #16]\n\t" "str d11, [%0, #24]\n\t"
        "str d12, [%0, #32]\n\t" "str d13, [%0, #40]\n\t"
        "str d14, [%0, #48]\n\t" "str d15, [%0, #56]\n\t"
        : : "r"(v) : "memory");
}

static const double fp_a_want[8] = { 11, 12, 13, 14, 15, 16, 17, 18 };
static const double fp_b_want[8] = { 101, 102, 103, 104, 105, 106, 107, 108 };
static double fp_a_got[8];
static double fp_b_got[8];

static void fp_coro_a(void *arg) {
    (void)arg;
    fp_load8(fp_a_want);    /* d8-d15 = A's pattern */
    lc_coroutine_yield();   /* B runs, sets d8-d15 = B's pattern, yields back */
    fp_store8(fp_a_got);    /* must still read A's pattern */
}
static void fp_coro_b(void *arg) {
    (void)arg;
    fp_load8(fp_b_want);
    lc_coroutine_yield();
    fp_store8(fp_b_got);
}
#endif

static void test_coroutine_fp_callee_saved(void) {
#if defined(__aarch64__)
    for (int i = 0; i < 8; i++) { fp_a_got[i] = 0; fp_b_got[i] = 0; }

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *ca = lc_coroutine_create(&sched, fp_coro_a, NULL);
    lc_coroutine *cb = lc_coroutine_create(&sched, fp_coro_b, NULL);
    TEST_ASSERT_NOT_NULL(ca);
    TEST_ASSERT_NOT_NULL(cb);

    lc_scheduler_run(&sched);

    for (int i = 0; i < 8; i++) {
        TEST_ASSERT(fp_a_got[i] == fp_a_want[i]);  /* A kept its d8-d15 */
        TEST_ASSERT(fp_b_got[i] == fp_b_want[i]);  /* B kept its d8-d15 */
    }

    lc_scheduler_destroy(&sched);
#else
    /* No callee-saved SIMD registers on this ABI — nothing to preserve. */
    TEST_ASSERT(true);
#endif
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

/* ===== Stack alignment at coroutine entry ===== */

/*
 * Regression test for the coroutine entry-stack alignment. Both the System V
 * AMD64 and AArch64 ABIs require a 16-byte-aligned stack. The trampoline is
 * entered via a return-address jump; if the initial context stack pointer is
 * off by 8, the whole coroutine call tree runs misaligned and any aligned SSE
 * spill (movaps) faults.
 *
 * lc_probe_entry_sp() is a *naked* function (no prologue at any optimization
 * level) that reads the stack pointer as its first instruction, i.e. the value
 * on entry via `call`/`bl`. The ABI pins that value:
 *   x86_64  — the caller leaves rsp 16-aligned before `call`, which pushes an
 *             8-byte return address, so a correctly-aligned frame gives
 *             rsp & 15 == 8.
 *   aarch64 — the return address lives in lr (nothing pushed), so a correctly
 *             aligned frame gives sp & 15 == 0.
 * A trampoline that mis-set the stack by 8 flips this modulus, so the check
 * discriminates the bug cleanly without provoking a fault.
 */
#if defined(__x86_64__)
__attribute__((naked, noinline)) static unsigned long lc_probe_entry_sp(void) {
    __asm__ volatile ("mov %rsp, %rax\n\tret");
}
#define LC_ENTRY_SP_MOD 8u
#elif defined(__aarch64__)
__attribute__((naked, noinline)) static unsigned long lc_probe_entry_sp(void) {
    __asm__ volatile ("mov x0, sp\n\tret");
}
#define LC_ENTRY_SP_MOD 0u
#else
static unsigned long lc_probe_entry_sp(void) { return 0u; }
#define LC_ENTRY_SP_MOD 0u
#endif

static int32_t align_probe_ok;
static int32_t align_probe_ran;

static void alignment_probe_func(void *arg) {
    (void)arg;
    align_probe_ok  = (lc_probe_entry_sp() & 15u) == LC_ENTRY_SP_MOD;
    align_probe_ran = 1;
}

static void test_coroutine_stack_alignment(void) {
    align_probe_ok = 0;
    align_probe_ran = 0;

    lc_scheduler sched = lc_scheduler_create();
    lc_coroutine *co = lc_coroutine_create(&sched, alignment_probe_func, NULL);
    TEST_ASSERT_NOT_NULL(co);

    lc_scheduler_run(&sched);

    TEST_ASSERT_EQ(align_probe_ran, 1);
    TEST_ASSERT_EQ(align_probe_ok, 1);  /* 16-byte-aligned local => aligned frame */

    lc_scheduler_destroy(&sched);
}

/* ===== M2: scheduler lifecycle hazards ===== */

/* Yield with no scheduler running on this thread must be a safe no-op, not a
 * NULL-deref of the current-scheduler TLS slot. Must run before any scheduler
 * has run on this thread, so it is registered first in main(). */
static void test_coroutine_yield_no_scheduler(void) {
    lc_coroutine_yield();
    lc_coroutine_yield();
    TEST_ASSERT(true);  /* reaching here without a crash is the assertion */
}

/* Re-running a scheduler whose coroutines have all finished must be a no-op —
 * never resurrect a FINISHED coroutine (its context sits past its final yield,
 * so resuming it is UB). */
static int32_t rerun_exec_count;  /* coroutines are cooperative — plain int is fine */
static void rerun_coro(void *arg) {
    (void)arg;
    rerun_exec_count++;
}

static void test_coroutine_scheduler_rerun(void) {
    rerun_exec_count = 0;

    lc_scheduler sched = lc_scheduler_create();
    TEST_ASSERT_NOT_NULL(lc_coroutine_create(&sched, rerun_coro, NULL));
    TEST_ASSERT_NOT_NULL(lc_coroutine_create(&sched, rerun_coro, NULL));

    lc_scheduler_run(&sched);
    TEST_ASSERT_EQ(rerun_exec_count, 2);  /* both ran exactly once */

    /* Second run: nothing runnable remains — no re-exec, no UB, no crash. */
    lc_scheduler_run(&sched);
    TEST_ASSERT_EQ(rerun_exec_count, 2);

    lc_scheduler_destroy(&sched);
}

/* A capacity whose backing array can't be allocated must leave the scheduler
 * unusable (capacity 0) so create() refuses, rather than writing through NULL. */
static void test_coroutine_scheduler_oom(void) {
    lc_scheduler sched = lc_scheduler_create_with_capacity(0xFFFFFFFFu);
    TEST_ASSERT_NULL(sched.coroutines);
    TEST_ASSERT_EQ(sched.capacity, (uint32_t)0);

    lc_coroutine *co = lc_coroutine_create(&sched, rerun_coro, NULL);
    TEST_ASSERT_NULL(co);  /* refused — no wild write through a NULL array */

    lc_scheduler_destroy(&sched);  /* safe on an empty scheduler */
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* M2: yield with no scheduler — FIRST, before any scheduler runs here */
    TEST_RUN(test_coroutine_yield_no_scheduler);

    /* scheduler lifecycle */
    TEST_RUN(test_scheduler_create_destroy);

    /* single coroutine */
    TEST_RUN(test_single_coroutine);

    /* multiple coroutines */
    TEST_RUN(test_multiple_coroutines);

    /* yield interleaving */
    TEST_RUN(test_coroutine_yield_interleaved);

    TEST_RUN(test_coroutine_fp_callee_saved);

    /* coroutine with argument */
    TEST_RUN(test_coroutine_with_argument);

    /* max coroutines */
    TEST_RUN(test_max_coroutines);

    /* over capacity */
    TEST_RUN(test_over_capacity);

    /* M2: scheduler lifecycle hazards */
    TEST_RUN(test_coroutine_scheduler_rerun);
    TEST_RUN(test_coroutine_scheduler_oom);

    /* entry stack alignment (16-byte ABI) */
    TEST_RUN(test_coroutine_stack_alignment);

    return test_main();
}
