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

/*
 * Deliver a signal to ourselves and return from the handler. This exercises
 * the x86_64 SA_RESTORER trampoline (signal_restorer -> rt_sigreturn): the
 * kernel enters the restorer with %rsp pointing at the signal frame, so the
 * restorer must be prologue-free. If it isn't (e.g. a plain C function at -O0
 * emits `push %rbp`), rt_sigreturn reads the frame at the wrong address and
 * the resumed context is corrupt — the process crashes instead of continuing
 * past the kill() below. Reaching the asserts with both flags set proves the
 * handler ran and control returned cleanly through the restorer.
 */
static volatile int32_t roundtrip_ran;

static void roundtrip_handler(int signo) {
    (void)signo;
    roundtrip_ran = 1;
}

static void test_signal_delivery_roundtrip(void) {
    roundtrip_ran = 0;

    TEST_ASSERT_OK(lc_signal_handle(SIGUSR1, roundtrip_handler));

    /* kill() to self delivers the (unblocked) signal synchronously: the
     * handler runs and returns via the restorer before the syscall returns. */
    int32_t pid = lc_kernel_get_process_id();
    lc_kernel_send_signal(pid, SIGUSR1);

    int32_t resumed_ok = 1;  /* set only if we get here — i.e. resume worked */

    TEST_ASSERT_EQ(roundtrip_ran, 1);
    TEST_ASSERT_EQ(resumed_ok, 1);

    (void)lc_signal_reset(SIGUSR1);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    TEST_RUN(test_signal_handle_register);
    TEST_RUN(test_signal_block_unblock);
    TEST_RUN(test_signal_ignore);
    TEST_RUN(test_signal_reset_call);
    TEST_RUN(test_signal_delivery_roundtrip);

    return test_main();
}
