#include <lightc/signal.h>
#include <lightc/syscall.h>

/* SA_RESTORER flag — required on x86_64, not used on aarch64 */
#define SA_RESTORER 0x04000000

/*
 * x86_64 requires a restorer trampoline that calls rt_sigreturn.
 * The kernel does not provide one automatically on this architecture.
 */
#if defined(__x86_64__)
static void signal_restorer(void) {
    __asm__ volatile("mov $15, %%rax\n\tsyscall" ::: "rax", "rcx", "r11");
}
#endif

/*
 * Internal helper: install a sigaction with the given handler and flags.
 * Handles arch-specific restorer setup.
 */
static lc_result signal_set_action(int signo, void *handler) {
    lc_kernel_sigaction sa = {0};
    sa.handler = handler;
    sa.mask    = 0;

#if defined(__x86_64__)
    sa.flags    = SA_RESTORER;
    sa.restorer = (void *)(uintptr_t)signal_restorer;
#else
    sa.flags = 0;
#endif

    return lc_result_from_sysret(lc_syscall4(SYS_rt_sigaction, signo, (int64_t)&sa, 0, 8));
}

lc_result lc_signal_handle(int signo, lc_signal_handler handler) {
    return signal_set_action(signo, (void *)(uintptr_t)handler);
}

lc_result lc_signal_block(int signo) {
    uint64_t mask = (uint64_t)1 << (signo - 1);
    return lc_result_from_sysret(lc_kernel_set_signal_mask(LC_SIG_BLOCK, &mask, NULL));
}

lc_result lc_signal_unblock(int signo) {
    uint64_t mask = (uint64_t)1 << (signo - 1);
    return lc_result_from_sysret(lc_kernel_set_signal_mask(LC_SIG_UNBLOCK, &mask, NULL));
}

void lc_on_crash(lc_crash_handler handler) {
    (void)lc_signal_handle(LC_SIGSEGV, handler);
    (void)lc_signal_handle(LC_SIGBUS,  handler);
    (void)lc_signal_handle(LC_SIGABRT, handler);
}

void lc_on_shutdown(lc_signal_handler handler) {
    (void)lc_signal_handle(LC_SIGTERM, handler);
    (void)lc_signal_handle(LC_SIGINT,  handler);
}

lc_result lc_signal_ignore(int signo) {
    return signal_set_action(signo, SIG_IGN);
}

lc_result lc_signal_reset(int signo) {
    lc_kernel_sigaction sa = {0};
    sa.handler = SIG_DFL;
    sa.flags   = 0;
#if defined(__x86_64__)
    sa.restorer = NULL;
#endif
    sa.mask = 0;
    return lc_result_from_sysret(lc_syscall4(SYS_rt_sigaction, signo, (int64_t)&sa, 0, 8));
}
