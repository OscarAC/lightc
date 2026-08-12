#include <lightc/lifecycle.h>
#include <lightc/signal.h>
#include <lightc/syscall.h>
#include <stdatomic.h>

static lc_atexit_func handlers[LC_MAX_ATEXIT_HANDLERS];
static uint32_t handler_count = 0;

/* Set once lc_exit has begun, so shutdown is performed exactly once regardless
 * of how it is re-entered (a signal arriving mid-shutdown, or a handler that
 * itself calls lc_exit). */
static _Atomic(int32_t) exiting = 0;

lc_result lc_atexit(lc_atexit_func func) {
    if (handler_count >= LC_MAX_ATEXIT_HANDLERS)
        return lc_err(LC_ERR_FULL);
    handlers[handler_count++] = func;
    return lc_ok(0);
}

void lc_exit(int code) {
    /* Reentrancy guard: only the first caller runs the handler chain. A second
     * entry — a signal delivered while a handler is running, or a handler that
     * calls lc_exit — must NOT re-run the chain (that double-closes/double-frees
     * everything the handlers touch). It terminates immediately instead. */
    int32_t expected = 0;
    if (!atomic_compare_exchange_strong(&exiting, &expected, 1)) {
        lc_kernel_exit(code);
    }

    /* Run handlers in reverse (LIFO) order, exactly once */
    uint32_t i = handler_count;
    while (i > 0) {
        --i;
        handlers[i]();
    }
    lc_kernel_exit(code);
}

static void shutdown_handler(int signo) {
    (void)signo;
    /* Ignore repeat shutdown signals that arrive while lc_exit is already
     * running, so the in-progress shutdown finishes rather than being cut
     * short. lc_exit's CAS is the hard backstop against re-running handlers. */
    if (atomic_load(&exiting))
        return;
    lc_exit(0);
}

void lc_lifecycle_enable_shutdown_signals(void) {
    lc_on_shutdown(shutdown_handler);
}
