#ifndef LIGHTC_LIFECYCLE_H
#define LIGHTC_LIFECYCLE_H

#include <lightc/types.h>

/* Maximum number of atexit handlers */
#define LC_MAX_ATEXIT_HANDLERS 32

/* Register a cleanup function to be called on exit */
typedef void (*lc_atexit_func)(void);

/* Register a cleanup function. */
[[nodiscard]] lc_result lc_atexit(lc_atexit_func func);

/* Exit the process, running all registered atexit handlers first.
 * Handlers are called in reverse registration order (LIFO). */
void lc_exit(int code) __attribute__((noreturn));

/* Install signal handlers so SIGTERM/SIGINT trigger the atexit chain.
 * Must be called after setting up any atexit handlers. */
void lc_lifecycle_enable_shutdown_signals(void);

#endif
