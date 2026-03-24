/*
 * test_lifecycle.c — tests for lightc atexit registration.
 *
 * We test registration only — calling lc_exit would terminate the
 * test process, so we cannot verify handler execution here.
 */

#include "test.h"
#include <lightc/lifecycle.h>

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

    TEST_RUN(test_atexit_register);
    TEST_RUN(test_atexit_overflow);

    return test_main();
}
