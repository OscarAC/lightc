#ifndef LIGHTIO_H
#define LIGHTIO_H

#include <lightc/types.h>

/*
 * lightio — ultrafast async I/O framework.
 *
 * Coroutines + io_uring. User code looks synchronous but runs fully async.
 * Each connection gets its own coroutine. Read/write suspend the coroutine.
 * The event loop drives io_uring and resumes coroutines on completion.
 *
 *   void handle(lio_stream *s) {
 *       char buf[4096];
 *       int32_t n = lio_read(s, buf, sizeof(buf));
 *       lio_write(s, response, len);
 *   }
 *
 *   lio_loop *loop = lio_loop_create();
 *   lio_tcp_serve(loop, 8080, handle);
 *   lio_loop_run(loop);
 */

/* Forward declarations */
typedef struct lio_loop lio_loop;
typedef struct lio_stream lio_stream;

/* Connection handler — called in its own coroutine */
typedef void (*lio_handler)(lio_stream *stream);

/* --- Event Loop --- */

lio_loop *lio_loop_create(void);
void lio_loop_destroy(lio_loop *loop);
void lio_loop_run(lio_loop *loop);
void lio_loop_stop(lio_loop *loop);

/* Run with N worker threads. Each worker gets its own io_uring ring
 * and coroutine pool. The kernel distributes connections across workers.
 * Call lio_tcp_serve() first, then this instead of lio_loop_run().
 * thread_count=0 means auto-detect (one per CPU, capped at 16). */
void lio_loop_run_threaded(lio_loop *loop, uint32_t thread_count);

/* --- TCP Server --- */

/* Listen on port and call handler for each connection (one coroutine each). */
bool lio_tcp_serve(lio_loop *loop, uint16_t port, lio_handler handler);

/* --- Async I/O (call from within a handler coroutine) --- */

/* Read up to count bytes. Returns bytes read, 0 on EOF, negative on error. */
int32_t lio_read(lio_stream *stream, void *buf, uint32_t count);

/* Write count bytes. Returns bytes written or negative on error. */
int32_t lio_write(lio_stream *stream, const void *buf, uint32_t count);

/* Close the stream. */
void lio_close(lio_stream *stream);

/* Get the file descriptor (e.g., for logging). */
int32_t lio_stream_fd(const lio_stream *stream);

/* --- Timer --- */

/* Sleep for the given number of milliseconds. Suspends the coroutine. */
void lio_sleep(lio_stream *stream, int64_t milliseconds);

#endif /* LIGHTIO_H */
