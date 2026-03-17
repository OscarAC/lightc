#ifndef LIGHTC_SIGNAL_H
#define LIGHTC_SIGNAL_H

#include <lightc/types.h>

/* Signal numbers (Linux x86_64/aarch64) */
#define LC_SIGINT    2
#define LC_SIGQUIT   3
#define LC_SIGABRT   6
#define LC_SIGBUS    7
#define LC_SIGSEGV  11
#define LC_SIGPIPE  13
#define LC_SIGALRM  14
#define LC_SIGTERM  15

/* Signal handler types */
typedef void (*lc_signal_handler)(int signo);
typedef void (*lc_crash_handler)(int signo);

/* Install a handler for a specific signal */
bool lc_signal_handle(int signo, lc_signal_handler handler);

/* Block/unblock a signal */
bool lc_signal_block(int signo);
bool lc_signal_unblock(int signo);

/* Convenience: install handler for crash signals (SIGSEGV, SIGBUS, SIGABRT) */
void lc_on_crash(lc_crash_handler handler);

/* Convenience: install handler for shutdown signals (SIGTERM, SIGINT) */
void lc_on_shutdown(lc_signal_handler handler);

/* Ignore a signal */
bool lc_signal_ignore(int signo);

/* Reset a signal to default behavior */
bool lc_signal_reset(int signo);

#endif
