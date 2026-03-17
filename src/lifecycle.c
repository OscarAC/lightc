#include <lightc/lifecycle.h>
#include <lightc/signal.h>
#include <lightc/syscall.h>

static lc_atexit_func handlers[LC_MAX_ATEXIT_HANDLERS];
static uint32_t handler_count = 0;

/* Guard against re-entrant shutdown (e.g. handler triggers another signal) */
static volatile int shutting_down = 0;

bool lc_atexit(lc_atexit_func func) {
    if (handler_count >= LC_MAX_ATEXIT_HANDLERS)
        return false;
    handlers[handler_count++] = func;
    return true;
}

void lc_exit(int code) {
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
    if (shutting_down)
        return;
    shutting_down = 1;
    lc_exit(0);
}

void lc_lifecycle_enable_shutdown_signals(void) {
    lc_on_shutdown(shutdown_handler);
}
