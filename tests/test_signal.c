/*
 * test_signal.c — tests for lightc signal handling.
 *
 * Signal tests are limited because signal delivery depends on
 * the runtime environment (containers may restrict signals).
 * We test registration succeeds without triggering signals.
 */

#include "test.h"
#include <lightc/signal.h>
#include <lightc/syscall.h>

static void dummy_handler(int signo) {
    (void)signo;
}

/* Test that signal handler registration succeeds */
static void test_signal_handle_register(void) {
    TEST_ASSERT_OK(lc_signal_handle(SIGUSR1, dummy_handler));
    (void)lc_signal_reset(SIGUSR1);
}

/* Test that block/unblock succeed */
static void test_signal_block_unblock(void) {
    TEST_ASSERT_OK(lc_signal_block(SIGUSR1));
    TEST_ASSERT_OK(lc_signal_unblock(SIGUSR1));
}

/* Test that ignore succeeds */
static void test_signal_ignore(void) {
    TEST_ASSERT_OK(lc_signal_ignore(SIGUSR1));
    (void)lc_signal_reset(SIGUSR1);
}

/* Test that reset succeeds */
static void test_signal_reset_call(void) {
    TEST_ASSERT_OK(lc_signal_handle(SIGUSR1, dummy_handler));
    TEST_ASSERT_OK(lc_signal_reset(SIGUSR1));
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    TEST_RUN(test_signal_handle_register);
    TEST_RUN(test_signal_block_unblock);
    TEST_RUN(test_signal_ignore);
    TEST_RUN(test_signal_reset_call);

    return test_main();
}
