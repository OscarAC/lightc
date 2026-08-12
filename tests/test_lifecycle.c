/*
 * test_lifecycle.c — tests for lightc atexit registration.
 *
 * We test registration only — calling lc_exit would terminate the
 * test process, so we cannot verify handler execution here.
 */

#include "test.h"
#include <lightc/lifecycle.h>
#include <lightc/syscall.h>

/* ===== M3: lc_exit reentrancy guard =====
 *
 * lc_exit terminates the process, so we observe handler execution from a forked
 * child through a MAP_SHARED page, then reap the child and inspect the counts.
 *
 * The handler re-enters lc_exit exactly once. With the reentrancy guard the
 * re-entrant call terminates immediately, so the handler runs once (count == 1).
 * Without it, the re-entrant lc_exit re-runs the whole handler chain from the
 * top, so the handler runs twice (count == 2) — the double-close/double-free bug.
 */
static volatile int32_t *exit_counter;  /* [0] = executions, [1] = re-enter latch */

static void reentrant_exit_handler(void) {
    exit_counter[0]++;
    if (exit_counter[1] == 0) {
        exit_counter[1] = 1;
        lc_exit(0);  /* re-enter — noreturn */
    }
}

static void test_exit_reentrancy_guard(void) {
    void *page = lc_kernel_map_memory(NULL, 4096, PROT_READ | PROT_WRITE,
                                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(page != MAP_FAILED);
    exit_counter = (volatile int32_t *)page;
    exit_counter[0] = 0;
    exit_counter[1] = 0;

    lc_sysret pid = lc_kernel_fork();
    TEST_ASSERT(pid >= 0);
    if (pid == 0) {
        /* Child: register the re-entering handler and exit. lc_exit is noreturn,
         * so the child never falls back into the test harness. */
        (void)lc_atexit(reentrant_exit_handler);
        lc_exit(0);
    }

    /* Parent: reap the child, then check how many times the handler ran. */
    int32_t status = 0;
    lc_kernel_wait_for_child((int32_t)pid, &status, 0);

    TEST_ASSERT_EQ(exit_counter[0], 1);  /* handler ran exactly once */

    lc_kernel_unmap_memory(page, 4096);
}

/* ===== test_atexit_register ===== */

static void dummy_atexit_handler(void) {
    /* intentionally empty */
}

static void test_atexit_register(void) {
    TEST_ASSERT_OK(lc_atexit(dummy_atexit_handler));
}

/* ===== test_atexit_overflow ===== */

static void overflow_handler_0(void) {}
static void overflow_handler_1(void) {}

static void test_atexit_overflow(void) {
    /*
     * One handler was already registered in test_atexit_register above.
     * Fill remaining slots. We alternate between two different function
     * pointers to avoid any dedup the implementation might do.
     */
    int registered = 1; /* account for the one already registered */

    for (; registered < LC_MAX_ATEXIT_HANDLERS; registered++) {
        lc_atexit_func fn = (registered % 2 == 0)
            ? overflow_handler_0
            : overflow_handler_1;
        lc_result r = lc_atexit(fn);
        if (r.error != 0) {
            /* Registration failed before we expected — report it */
            TEST_ASSERT_OK(r);
            return;
        }
    }

    /* Table should now be full — next registration must fail */
    TEST_ASSERT_ERR(lc_atexit(dummy_atexit_handler));
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Run first, while the atexit table still has room for the child to register
     * its handler (test_atexit_overflow fills the table afterwards). */
    TEST_RUN(test_exit_reentrancy_guard);

    TEST_RUN(test_atexit_register);
    TEST_RUN(test_atexit_overflow);

    return test_main();
}
