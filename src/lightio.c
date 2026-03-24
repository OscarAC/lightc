#include <lightio/lightio.h>
#include <lightc/async.h>
#include <lightc/coroutine.h>
#include <lightc/socket.h>
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/heap.h>
#include <lightc/print.h>
#include <lightc/thread.h>
#include <lightc/time.h>
#include <stdatomic.h>

/* ========================================================================
 * io_uring opcodes needed by lightio
 * ======================================================================== */

#define IORING_OP_TIMEOUT  11
#define IORING_OP_ACCEPT   13

/* ========================================================================
 * Constants
 * ======================================================================== */

#define LIO_MAX_SLOTS     1024
#define LIO_STACK_SIZE    (64 * 1024)   /* 64 KiB per coroutine */
#define LIO_RING_SIZE     512
#define LIO_TAG_ACCEPT    0xFFFFFFFFULL

/* Slot states */
#define SLOT_FREE     0
#define SLOT_READY    1
#define SLOT_RUNNING  2
#define SLOT_WAITING  3
#define SLOT_FINISHED 4

/* ========================================================================
 * Structures
 * ======================================================================== */

/* Exposed to user code */
struct lio_stream {
    lio_loop *loop;
    int32_t   slot;
    int32_t   fd;
};

/* Internal: one slot per connection */
typedef struct {
    int32_t              state;
    int32_t              fd;
    void                *stack_base;
    int32_t              pending_result;
    lc_coroutine_context context;
    lio_handler          handler;
    lio_stream           stream;
} lio_slot;

/* Kernel timespec layout for IORING_OP_TIMEOUT */
typedef struct {
    int64_t tv_sec;
    int64_t tv_nsec;
} lio_kernel_timespec;

/* The event loop */
struct lio_loop {
    lc_async_ring       *ring;
    lc_coroutine_context main_context;   /* event loop's saved context */

    lio_slot             slots[LIO_MAX_SLOTS];
    int32_t              current_slot;   /* which coroutine is running (-1 = event loop) */
    uint32_t             active_count;   /* number of active coroutines */
    bool                 running;

    /* TCP server */
    int32_t              server_fd;
    uint16_t             server_port;
    lio_handler          accept_handler;

    /* Timeout support — one timespec per slot (for lio_sleep) */
    lio_kernel_timespec  timeout_specs[LIO_MAX_SLOTS];
};

/* ========================================================================
 * Assembly context switch — defined in arch/{x86_64,aarch64}/coroutine.S
 * ======================================================================== */

extern void lc_coroutine_switch(lc_coroutine_context *save,
                                lc_coroutine_context *restore);

/* ========================================================================
 * Per-thread: the trampoline needs to know which loop is current
 * ======================================================================== */

static lc_tls_key loop_tls_key;
static lc_once loop_tls_init = LC_ONCE_INIT;

static void init_loop_tls(void) {
    lc_tls_key_create(&loop_tls_key);
}

static lio_loop *get_current_loop(void) {
    lc_call_once(&loop_tls_init, init_loop_tls);
    return (lio_loop *)lc_tls_get(loop_tls_key);
}

static void set_current_loop(lio_loop *loop) {
    lc_call_once(&loop_tls_init, init_loop_tls);
    lc_tls_set(loop_tls_key, loop);
}

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static int32_t alloc_slot(lio_loop *loop, int32_t fd) {
    for (int32_t i = 0; i < LIO_MAX_SLOTS; i++) {
        if (loop->slots[i].state == SLOT_FREE) {
            loop->slots[i].state          = SLOT_READY;
            loop->slots[i].fd             = fd;
            loop->slots[i].pending_result = 0;
            loop->slots[i].stream.loop    = loop;
            loop->slots[i].stream.slot    = i;
            loop->slots[i].stream.fd      = fd;
            loop->slots[i].handler        = loop->accept_handler;
            loop->active_count++;
            return i;
        }
    }
    return -1;
}

/*
 * Coroutine entry point. When lc_coroutine_switch "returns" into a freshly
 * initialized context, execution begins here. We call the user handler,
 * mark the slot finished, and switch back to the event loop.
 */
static void connection_trampoline(void) {
    lio_loop *loop = get_current_loop();
    int32_t slot   = loop->current_slot;
    lio_slot *s    = &loop->slots[slot];

    /* Run the user's handler */
    s->handler(&s->stream);

    /* Mark finished and return to event loop */
    s->state = SLOT_FINISHED;
    lc_coroutine_switch(&s->context, &loop->main_context);
    __builtin_unreachable();
}

static void init_coroutine_for_slot(lio_loop *loop, int32_t slot) {
    lio_slot *s = &loop->slots[slot];

    /* Allocate stack via mmap */
    s->stack_base = lc_kernel_map_memory(NULL, LIO_STACK_SIZE,
                                         PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (s->stack_base == MAP_FAILED) {
        s->stack_base = NULL;
        s->state = SLOT_FINISHED;
        return;
    }

    /* Zero the context */
    lc_bytes_fill(&s->context, 0, sizeof(s->context));

    /*
     * Set up the initial context so that lc_coroutine_switch "returns"
     * into connection_trampoline.
     */
#if defined(__x86_64__)
    uint64_t *stack_top = (uint64_t *)((uint8_t *)s->stack_base + LIO_STACK_SIZE);

    /* Push trampoline as return address */
    stack_top--;
    *stack_top = (uint64_t)connection_trampoline;

    s->context.rsp = (uint64_t)stack_top;
    s->context.rbp = 0;
    s->context.rbx = 0;
    s->context.r12 = 0;
    s->context.r13 = 0;
    s->context.r14 = 0;
    s->context.r15 = 0;
#elif defined(__aarch64__)
    uint64_t *stack_top = (uint64_t *)((uint8_t *)s->stack_base + LIO_STACK_SIZE);
    stack_top = (uint64_t *)((uintptr_t)stack_top & ~15ULL);

    s->context.sp  = (uint64_t)stack_top;
    s->context.x30 = (uint64_t)connection_trampoline;  /* lr */
    s->context.x29 = 0;                                /* fp */
    s->context.x19 = 0; s->context.x20 = 0;
    s->context.x21 = 0; s->context.x22 = 0;
    s->context.x23 = 0; s->context.x24 = 0;
    s->context.x25 = 0; s->context.x26 = 0;
    s->context.x27 = 0; s->context.x28 = 0;
#endif
}

/* ========================================================================
 * Public API: Event Loop
 * ======================================================================== */

lio_loop *lio_loop_create(void) {
    lc_result_ptr loop_alloc = lc_heap_allocate(sizeof(lio_loop));
    if (lc_ptr_is_err(loop_alloc)) return NULL;
    lio_loop *loop = loop_alloc.value;

    lc_bytes_fill(loop, 0, sizeof(*loop));

    lc_result_ptr ring_alloc = lc_async_ring_create(LIO_RING_SIZE);
    if (lc_ptr_is_err(ring_alloc)) {
        lc_heap_free(loop);
        return NULL;
    }
    loop->ring = ring_alloc.value;

    /* Initialize all slots as free */
    for (int32_t i = 0; i < LIO_MAX_SLOTS; i++) {
        loop->slots[i].state      = SLOT_FREE;
        loop->slots[i].fd         = -1;
        loop->slots[i].stack_base = NULL;
    }

    loop->current_slot  = -1;
    loop->active_count  = 0;
    loop->running       = false;
    loop->server_fd     = -1;
    loop->accept_handler = NULL;

    return loop;
}

void lio_loop_destroy(lio_loop *loop) {
    if (!loop) return;

    /* Close server socket */
    if (loop->server_fd >= 0) {
        lc_kernel_close_file(loop->server_fd);
    }

    /* Free all active slot stacks */
    for (int32_t i = 0; i < LIO_MAX_SLOTS; i++) {
        if (loop->slots[i].state != SLOT_FREE && loop->slots[i].stack_base) {
            lc_kernel_unmap_memory(loop->slots[i].stack_base, LIO_STACK_SIZE);
        }
    }

    lc_async_ring_destroy(loop->ring);
    lc_heap_free(loop);
}

void lio_loop_stop(lio_loop *loop) {
    loop->running = false;
}

void lio_loop_run(lio_loop *loop) {
    loop->running = true;
    set_current_loop(loop);

    while (loop->running) {
        /* 1. Run all READY coroutines */
        for (int32_t i = 0; i < LIO_MAX_SLOTS; i++) {
            if (loop->slots[i].state == SLOT_READY) {
                loop->current_slot = i;
                set_current_loop(loop);
                loop->slots[i].state = SLOT_RUNNING;
                lc_coroutine_switch(&loop->main_context, &loop->slots[i].context);

                /* Back from coroutine — it either yielded (WAITING) or finished */
                if (loop->slots[i].state == SLOT_FINISHED) {
                    if (loop->slots[i].fd >= 0) {
                        lc_kernel_close_file(loop->slots[i].fd);
                        loop->slots[i].fd = -1;
                    }
                    if (loop->slots[i].stack_base) {
                        lc_kernel_unmap_memory(loop->slots[i].stack_base, LIO_STACK_SIZE);
                        loop->slots[i].stack_base = NULL;
                    }
                    loop->slots[i].state = SLOT_FREE;
                    loop->active_count--;
                }
            }
        }

        /* Stop check: if running was set to false while processing coroutines */
        if (!loop->running) break;

        /* 2. Flush io_uring submissions */
        lc_async_flush(loop->ring);

        /* 3. Wait for completions (only if there's work to do) */
        lc_async_result results[64];
        uint32_t n = 0;

        if (loop->active_count > 0 || loop->server_fd >= 0) {
            n = lc_async_wait(loop->ring, results, 64);
        } else {
            /* Nothing left to do */
            break;
        }

        /* 4. Process completions */
        for (uint32_t i = 0; i < n; i++) {
            if (results[i].tag == LIO_TAG_ACCEPT) {
                /* New connection accepted */
                int32_t client_fd = results[i].result;
                if (client_fd >= 0) {
                    int32_t slot = alloc_slot(loop, client_fd);
                    if (slot >= 0) {
                        init_coroutine_for_slot(loop, slot);
                    } else {
                        lc_kernel_close_file(client_fd);
                    }
                }
                /* Re-submit accept for next connection */
                if (loop->running) {
                    (void)lc_async_submit_raw(loop->ring, IORING_OP_ACCEPT,
                                              loop->server_fd, 0, 0, 0, 0,
                                              LIO_TAG_ACCEPT);
                }
            } else {
                /* I/O completion — resume the waiting coroutine */
                uint32_t slot = (uint32_t)results[i].tag;
                if (slot < LIO_MAX_SLOTS &&
                    loop->slots[slot].state == SLOT_WAITING) {
                    loop->slots[slot].pending_result = results[i].result;
                    loop->slots[slot].state = SLOT_READY;
                }
            }
        }
    }
}

/* ========================================================================
 * Public API: TCP Server
 * ======================================================================== */

lc_result lio_tcp_serve(lio_loop *loop, uint16_t port, lio_handler handler) {
    /* Create server socket */
    lc_result r = lc_socket_create(LC_SOCK_STREAM);
    if (lc_is_err(r)) return r;
    int32_t fd = (int32_t)r.value;

    /* Set reuse address */
    lc_socket_set_reuse_address(fd);

    /* Bind to port */
    lc_socket_address addr = lc_socket_address_any(port);
    lc_result bind_r = lc_socket_bind(fd, &addr);
    if (lc_is_err(bind_r)) {
        lc_socket_close(fd);
        return bind_r;
    }

    /* Listen */
    lc_result listen_r = lc_socket_listen(fd, 128);
    if (lc_is_err(listen_r)) {
        lc_socket_close(fd);
        return listen_r;
    }

    loop->server_fd      = fd;
    loop->server_port    = port;
    loop->accept_handler = handler;

    /* Submit initial accept via io_uring */
    (void)lc_async_submit_raw(loop->ring, IORING_OP_ACCEPT, fd,
                              0, 0, 0, 0, LIO_TAG_ACCEPT);

    return lc_ok(0);
}

/* ========================================================================
 * Public API: Async I/O (called from within a handler coroutine)
 * ======================================================================== */

int32_t lio_read(lio_stream *stream, void *buf, uint32_t count) {
    lio_loop *loop = stream->loop;
    int32_t slot   = stream->slot;

    /* Submit read to io_uring (offset -1 = current position / no offset) */
    (void)lc_async_submit_read(loop->ring, stream->fd, buf, count,
                               (uint64_t)-1, (uint64_t)slot);

    /* Suspend this coroutine */
    loop->slots[slot].state = SLOT_WAITING;
    lc_coroutine_switch(&loop->slots[slot].context, &loop->main_context);

    /* Resumed — return the result */
    return loop->slots[slot].pending_result;
}

int32_t lio_write(lio_stream *stream, const void *buf, uint32_t count) {
    uint32_t written = 0;

    while (written < count) {
        lio_loop *loop = stream->loop;
        int32_t slot   = stream->slot;

        (void)lc_async_submit_write(loop->ring, stream->fd,
                                    (const uint8_t *)buf + written,
                                    count - written, (uint64_t)-1, (uint64_t)slot);

        loop->slots[slot].state = SLOT_WAITING;
        lc_coroutine_switch(&loop->slots[slot].context, &loop->main_context);

        int32_t result = loop->slots[slot].pending_result;
        if (result <= 0) return result;  /* error or EOF */
        written += (uint32_t)result;
    }
    return (int32_t)written;
}

void lio_close(lio_stream *stream) {
    if (stream->fd >= 0) {
        lc_kernel_close_file(stream->fd);
        stream->fd = -1;
        /* Also update the slot's fd so the event loop doesn't double-close */
        stream->loop->slots[stream->slot].fd = -1;
    }
}

int32_t lio_stream_fd(const lio_stream *stream) {
    return stream->fd;
}

/* ========================================================================
 * Public API: Timer
 * ======================================================================== */

void lio_sleep(lio_stream *stream, int64_t milliseconds) {
    lio_loop *loop = stream->loop;
    int32_t slot   = stream->slot;

    /* Set up timeout spec */
    loop->timeout_specs[slot].tv_sec  = milliseconds / 1000;
    loop->timeout_specs[slot].tv_nsec = (milliseconds % 1000) * 1000000;

    /*
     * IORING_OP_TIMEOUT SQE layout:
     *   opcode = 11 (IORING_OP_TIMEOUT)
     *   fd     = -1 (unused)
     *   addr   = pointer to kernel_timespec
     *   len    = 1 (required by kernel)
     *   off    = 0 (pure timeout, no completion count)
     *   rw_flags = 0 (relative timeout)
     *
     * The CQE result will be -62 (ETIME) — that's normal for a timeout.
     */
    (void)lc_async_submit_raw(loop->ring, IORING_OP_TIMEOUT, -1,
                              (uint64_t)(uintptr_t)&loop->timeout_specs[slot],
                              1, 0, 0, (uint64_t)slot);

    loop->slots[slot].state = SLOT_WAITING;
    lc_coroutine_switch(&loop->slots[slot].context, &loop->main_context);
    /* Resumed after timeout */
}

/* ========================================================================
 * Public API: Multi-threaded event loop
 *
 * Each worker thread creates its own lio_loop with its own io_uring ring.
 * All workers share the same server fd and call IORING_OP_ACCEPT on it.
 * The kernel distributes incoming connections across workers.
 * Zero coordination, zero locks, linear scaling.
 * ======================================================================== */

#define LIO_MAX_WORKERS 16

typedef struct {
    int32_t     server_fd;
    lio_handler handler;
    uint32_t    worker_id;
    _Atomic(bool) *stop_flag;
} lio_worker_args;

static int32_t lio_worker_thread(void *arg) {
    lio_worker_args *wargs = (lio_worker_args *)arg;

    /* Each worker creates its own loop + io_uring ring */
    lio_loop *loop = lio_loop_create();
    if (!loop) return 1;

    /* Share the server fd — don't create a new socket */
    loop->server_fd = wargs->server_fd;
    loop->accept_handler = wargs->handler;

    /* Submit initial accept on the shared server fd */
    (void)lc_async_submit_raw(loop->ring, IORING_OP_ACCEPT,
                              loop->server_fd, 0, 0, 0, 0, LIO_TAG_ACCEPT);

    /* Run until stop flag is set */
    loop->running = true;
    set_current_loop(loop);

    while (loop->running && !atomic_load(wargs->stop_flag)) {
        /* Run ready coroutines */
        for (int32_t i = 0; i < LIO_MAX_SLOTS; i++) {
            if (loop->slots[i].state == SLOT_READY) {
                loop->current_slot = i;
                set_current_loop(loop);
                loop->slots[i].state = SLOT_RUNNING;
                lc_coroutine_switch(&loop->main_context, &loop->slots[i].context);

                if (loop->slots[i].state == SLOT_FINISHED) {
                    if (loop->slots[i].fd >= 0) {
                        lc_kernel_close_file(loop->slots[i].fd);
                        loop->slots[i].fd = -1;
                    }
                    if (loop->slots[i].stack_base) {
                        lc_kernel_unmap_memory(loop->slots[i].stack_base, LIO_STACK_SIZE);
                        loop->slots[i].stack_base = NULL;
                    }
                    loop->slots[i].state = SLOT_FREE;
                    loop->active_count--;
                }
            }
        }

        if (atomic_load(wargs->stop_flag)) break;

        /* Flush + wait */
        lc_async_flush(loop->ring);
        lc_async_result results[64];
        uint32_t n = lc_async_wait(loop->ring, results, 64);

        /* Process completions */
        for (uint32_t i = 0; i < n; i++) {
            if (results[i].tag == LIO_TAG_ACCEPT) {
                int32_t client_fd = results[i].result;
                if (client_fd >= 0) {
                    int32_t slot = alloc_slot(loop, client_fd);
                    if (slot >= 0) {
                        init_coroutine_for_slot(loop, slot);
                    } else {
                        lc_kernel_close_file(client_fd);
                    }
                }
                if (!atomic_load(wargs->stop_flag)) {
                    (void)lc_async_submit_raw(loop->ring, IORING_OP_ACCEPT,
                                              loop->server_fd, 0, 0, 0, 0,
                                              LIO_TAG_ACCEPT);
                }
            } else {
                uint32_t slot = (uint32_t)results[i].tag;
                if (slot < LIO_MAX_SLOTS &&
                    loop->slots[slot].state == SLOT_WAITING) {
                    loop->slots[slot].pending_result = results[i].result;
                    loop->slots[slot].state = SLOT_READY;
                }
            }
        }
    }

    /* Don't close server_fd — it's shared, the main thread owns it */
    loop->server_fd = -1;
    lc_async_ring_destroy(loop->ring);
    loop->ring = NULL;
    lc_heap_free(loop);
    return 0;
}

void lio_loop_run_threaded(lio_loop *loop, uint32_t thread_count) {
    if (thread_count == 0) thread_count = 4;  /* default */
    if (thread_count > LIO_MAX_WORKERS) thread_count = LIO_MAX_WORKERS;

    _Atomic(bool) stop_flag = false;

    /* Worker args — one per thread */
    lio_worker_args args[LIO_MAX_WORKERS];
    lc_thread threads[LIO_MAX_WORKERS];

    /* Spawn worker threads (all share the same server fd) */
    uint32_t created_count = 0;
    for (uint32_t i = 0; i < thread_count; i++) {
        args[i].server_fd = loop->server_fd;
        args[i].handler = loop->accept_handler;
        args[i].worker_id = i;
        args[i].stop_flag = &stop_flag;
        if (lc_is_ok(lc_thread_create(&threads[i], lio_worker_thread, &args[i]))) {
            created_count++;
        }
    }

    /* Main thread: wait. Workers run independently.
     * When lio_loop_stop is called, set the stop flag. */
    loop->running = true;
    while (loop->running) {
        lc_time_sleep_milliseconds(100);
    }

    /* Signal all workers to stop */
    atomic_store(&stop_flag, true);

    /* Wake workers blocked in io_uring_enter by connecting briefly.
     * Each connect triggers an accept completion, waking one worker. */
    for (uint32_t i = 0; i < created_count; i++) {
        lc_result wr = lc_socket_connect_to(127, 0, 0, 1, loop->server_port);
        if (!lc_is_err(wr)) lc_socket_close((int32_t)wr.value);
    }

    /* Join only the threads that were actually created */
    for (uint32_t i = 0; i < created_count; i++) {
        lc_thread_join(&threads[i]);
    }
}
