/*
 * httpd — a tiny async HTTP server built on lightc.
 *
 * No libc. Uses io_uring for async read/write on client connections.
 * Two threads: an accept thread handles incoming connections, the main
 * thread processes io_uring completions. A spinlock protects the
 * submission ring since both threads submit operations.
 *
 * Usage:
 *   ./httpd [port]     (default 8080)
 *
 * Endpoints:
 *   GET /       — HTML page with lightc info
 *   GET /stats  — JSON with request count and active connections
 *   GET /hello  — plain text greeting
 *   anything else — 404
 */

#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/socket.h>
#include <lightc/async.h>
#include <lightc/thread.h>
#include <stdatomic.h>

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define MAX_CONNECTIONS   64
#define REQUEST_BUF_SIZE  4096
#define RESPONSE_BUF_SIZE 8192
#define RING_QUEUE_SIZE   256

/* Shorthand for string literal + length */
#define S(literal) literal, sizeof(literal) - 1

/* io_uring user_data tag encoding: high 32 = slot index, low 32 = op type */
#define OP_READ  0
#define OP_WRITE 1
#define MAKE_TAG(slot, op)  (((uint64_t)(slot) << 32) | (uint64_t)(op))
#define TAG_SLOT(tag)       ((uint32_t)((tag) >> 32))
#define TAG_OP(tag)         ((uint32_t)((tag) & 0xFFFFFFFF))

/* SOCK_NONBLOCK for accept4 — makes the *accepted* client fd non-blocking.
 * We use it on the server socket type to make accept() itself non-blocking. */
#define SOCK_NONBLOCK  0x800

/* ========================================================================
 * Connection state
 * ======================================================================== */

typedef struct {
    int32_t  fd;
    bool     active;
    uint8_t  request[REQUEST_BUF_SIZE];
    uint8_t  response[RESPONSE_BUF_SIZE];
    size_t   response_len;
} connection;

static connection      connections[MAX_CONNECTIONS];
static _Atomic(uint64_t) request_count   = 0;
static _Atomic(int32_t)  active_count    = 0;
static _Atomic(bool)     server_running  = true;

/* Spinlock protecting the io_uring submission ring */
static lc_spinlock ring_lock = LC_SPINLOCK_INIT;

/* ========================================================================
 * Connection slot management
 * ======================================================================== */

static int32_t connection_allocate(int32_t client_fd) {
    for (int32_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (!connections[i].active) {
            connections[i].fd     = client_fd;
            connections[i].active = true;
            connections[i].response_len = 0;
            lc_bytes_fill(connections[i].request, 0, REQUEST_BUF_SIZE);
            atomic_fetch_add(&active_count, 1);
            return i;
        }
    }
    return -1;
}

static void connection_free(int32_t slot) {
    connections[slot].active = false;
    connections[slot].fd     = -1;
    atomic_fetch_sub(&active_count, 1);
}

/* ========================================================================
 * Integer to string helper (no printf!)
 * ======================================================================== */

/* Write decimal representation of value into buf.
 * Returns number of bytes written. */
static size_t uint_to_string(uint64_t value, uint8_t *buf, size_t buf_size) {
    if (value == 0) {
        if (buf_size > 0) buf[0] = '0';
        return 1;
    }

    /* Write digits in reverse */
    uint8_t tmp[20];
    size_t len = 0;
    while (value > 0 && len < sizeof(tmp)) {
        tmp[len++] = '0' + (uint8_t)(value % 10);
        value /= 10;
    }

    /* Reverse into output buffer */
    size_t written = 0;
    for (size_t i = len; i > 0 && written < buf_size; i--) {
        buf[written++] = tmp[i - 1];
    }
    return written;
}

/* ========================================================================
 * Simple port parser
 * ======================================================================== */

static uint16_t parse_port(const char *str) {
    uint16_t port = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] < '0' || str[i] > '9') return 0;
        port = port * 10 + (uint16_t)(str[i] - '0');
        if (port == 0 && i > 0) return 0;  /* overflow or leading zeros */
    }
    return port;
}

/* ========================================================================
 * HTTP response builder
 * ======================================================================== */

/* Append raw bytes to the response buffer. Returns new offset. */
static size_t response_append(uint8_t *buf, size_t buf_size,
                              size_t offset, const char *data, size_t len) {
    if (offset + len > buf_size) {
        len = buf_size - offset;
    }
    lc_bytes_copy(buf + offset, data, len);
    return offset + len;
}

static size_t build_response(uint8_t *buf, size_t buf_size,
                             const char *status, size_t status_len,
                             const char *content_type, size_t content_type_len,
                             const char *body, size_t body_len) {
    size_t pos = 0;

    /* Status line */
    pos = response_append(buf, buf_size, pos, S("HTTP/1.1 "));
    pos = response_append(buf, buf_size, pos, status, status_len);
    pos = response_append(buf, buf_size, pos, S("\r\n"));

    /* Content-Type */
    pos = response_append(buf, buf_size, pos, S("Content-Type: "));
    pos = response_append(buf, buf_size, pos, content_type, content_type_len);
    pos = response_append(buf, buf_size, pos, S("\r\n"));

    /* Content-Length */
    pos = response_append(buf, buf_size, pos, S("Content-Length: "));
    uint8_t len_str[20];
    size_t len_str_len = uint_to_string(body_len, len_str, sizeof(len_str));
    pos = response_append(buf, buf_size, pos, (const char *)len_str, len_str_len);
    pos = response_append(buf, buf_size, pos, S("\r\n"));

    /* Connection: close */
    pos = response_append(buf, buf_size, pos, S("Connection: close\r\n"));

    /* Server header */
    pos = response_append(buf, buf_size, pos, S("Server: lightc-httpd\r\n"));

    /* End of headers */
    pos = response_append(buf, buf_size, pos, S("\r\n"));

    /* Body */
    pos = response_append(buf, buf_size, pos, body, body_len);

    return pos;
}

/* ========================================================================
 * Response bodies
 * ======================================================================== */

static const char INDEX_BODY[] =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head><title>lightc httpd</title>\n"
    "<style>\n"
    "  body { font-family: monospace; background: #0a0a0a; color: #e0e0e0;\n"
    "         max-width: 640px; margin: 40px auto; padding: 0 20px; }\n"
    "  h1 { color: #f0c040; }\n"
    "  a { color: #60b0ff; }\n"
    "  .tag { background: #222; border: 1px solid #444;\n"
    "         padding: 2px 8px; border-radius: 4px; font-size: 0.85em; }\n"
    "</style></head>\n"
    "<body>\n"
    "<h1>lightc httpd</h1>\n"
    "<p>A tiny HTTP server with <b>zero libc dependencies</b>.</p>\n"
    "<p>\n"
    "  <span class=\"tag\">C23</span>\n"
    "  <span class=\"tag\">io_uring</span>\n"
    "  <span class=\"tag\">raw syscalls</span>\n"
    "  <span class=\"tag\">no libc</span>\n"
    "</p>\n"
    "<p>Built entirely on <b>lightc</b> &mdash; a freestanding C library that talks\n"
    "   directly to the Linux kernel via syscalls. No glibc, no musl, no\n"
    "   runtime overhead.</p>\n"
    "<h2>Endpoints</h2>\n"
    "<ul>\n"
    "  <li><a href=\"/\">/</a> &mdash; this page</li>\n"
    "  <li><a href=\"/stats\">/stats</a> &mdash; server statistics (JSON)</li>\n"
    "  <li><a href=\"/hello\">/hello</a> &mdash; a greeting</li>\n"
    "</ul>\n"
    "<h2>Architecture</h2>\n"
    "<ul>\n"
    "  <li>Accept thread: blocking <code>accept4</code> syscall</li>\n"
    "  <li>Main thread: <code>io_uring</code> completion processing</li>\n"
    "  <li>Async read/write on client sockets via io_uring</li>\n"
    "  <li>Spinlock-protected submission ring for thread safety</li>\n"
    "</ul>\n"
    "<p style=\"color:#666;margin-top:40px;\">"
    "lightc &mdash; no libc, no problem.</p>\n"
    "</body></html>\n";

static const char HELLO_BODY[] =
    "Hello from lightc httpd!\n\n"
    "This response was served without any C library.\n"
    "Just raw syscalls, io_uring, and a dream.\n";

static const char NOT_FOUND_BODY[] =
    "<!DOCTYPE html>\n"
    "<html><head><title>404</title>\n"
    "<style>body{font-family:monospace;background:#0a0a0a;color:#e0e0e0;"
    "text-align:center;padding-top:80px;}</style></head>\n"
    "<body><h1>404 &mdash; Not Found</h1>\n"
    "<p>Nothing here. Try <a href=\"/\" style=\"color:#60b0ff;\">/</a></p>\n"
    "</body></html>\n";

static const char BAD_REQUEST_BODY[] =
    "400 Bad Request\n";

/* Build a JSON stats response into the provided buffer.
 * Returns the number of bytes written. */
static size_t build_stats_body(uint8_t *buf, size_t buf_size) {
    size_t pos = 0;
    pos = response_append(buf, buf_size, pos, S("{\"requests\":"));

    uint8_t num[20];
    size_t num_len;

    num_len = uint_to_string(atomic_load(&request_count), num, sizeof(num));
    pos = response_append(buf, buf_size, pos, (const char *)num, num_len);

    pos = response_append(buf, buf_size, pos, S(",\"active_connections\":"));

    int32_t ac = atomic_load(&active_count);
    num_len = uint_to_string((uint64_t)(ac > 0 ? ac : 0), num, sizeof(num));
    pos = response_append(buf, buf_size, pos, (const char *)num, num_len);

    pos = response_append(buf, buf_size, pos, S("}\n"));
    return pos;
}

/* ========================================================================
 * HTTP request parsing
 * ======================================================================== */

/* Extract the request path from a raw HTTP request.
 * Looks for "GET /path HTTP/1.x\r\n".
 * On success: sets *path and *path_len, returns true.
 * Only supports GET. */
static bool parse_request(const uint8_t *request, size_t request_len,
                          const char **path, size_t *path_len,
                          bool *is_get) {
    *is_get   = false;
    *path     = "/";
    *path_len = 1;

    if (request_len < 14) return false;  /* minimum: "GET / HTTP/1.1\r\n" */

    /* Check method */
    if (request[0] == 'G' && request[1] == 'E' && request[2] == 'T' &&
        request[3] == ' ') {
        *is_get = true;
    } else {
        return false;  /* unsupported method */
    }

    /* Find path: starts at position 4, ends at next space */
    const char *start = (const char *)request + 4;
    size_t remaining  = request_len - 4;

    int64_t space_idx = lc_string_find_byte(start, remaining, ' ');
    if (space_idx <= 0) return false;

    *path     = start;
    *path_len = (size_t)space_idx;

    return true;
}

/* ========================================================================
 * Request handling — parse request, build response, submit async write
 * ======================================================================== */

static void handle_request(lc_async_ring *ring, int32_t slot, int32_t bytes_read) {
    connection *conn = &connections[slot];

    if (bytes_read <= 0) {
        /* Client disconnected or error — close silently */
        lc_socket_close(conn->fd);
        connection_free(slot);
        return;
    }

    atomic_fetch_add(&request_count, 1);

    /* Parse the request */
    const char *path;
    size_t path_len;
    bool is_get;

    bool parsed = parse_request(conn->request, (size_t)bytes_read,
                                &path, &path_len, &is_get);

    /* Log the request to stdout */
    lc_print_string(STDOUT, S("[httpd] "));
    if (parsed && is_get) {
        lc_print_string(STDOUT, S("GET "));
        lc_print_string(STDOUT, path, path_len);
    } else {
        lc_print_string(STDOUT, S("(bad request)"));
    }
    lc_print_string(STDOUT, S(" -> "));

    if (!parsed || !is_get) {
        /* 400 Bad Request */
        lc_print_line(STDOUT, S("400"));
        conn->response_len = build_response(
            conn->response, RESPONSE_BUF_SIZE,
            S("400 Bad Request"),
            S("text/plain"),
            BAD_REQUEST_BODY, sizeof(BAD_REQUEST_BODY) - 1);

    } else if (lc_string_equal(path, path_len, "/", 1)) {
        /* Root: HTML info page */
        lc_print_line(STDOUT, S("200"));
        conn->response_len = build_response(
            conn->response, RESPONSE_BUF_SIZE,
            S("200 OK"),
            S("text/html; charset=utf-8"),
            INDEX_BODY, sizeof(INDEX_BODY) - 1);

    } else if (lc_string_equal(path, path_len, S("/hello"))) {
        /* Greeting */
        lc_print_line(STDOUT, S("200"));
        conn->response_len = build_response(
            conn->response, RESPONSE_BUF_SIZE,
            S("200 OK"),
            S("text/plain"),
            HELLO_BODY, sizeof(HELLO_BODY) - 1);

    } else if (lc_string_equal(path, path_len, S("/stats"))) {
        /* Stats JSON */
        lc_print_line(STDOUT, S("200"));
        uint8_t stats_buf[256];
        size_t stats_len = build_stats_body(stats_buf, sizeof(stats_buf));
        conn->response_len = build_response(
            conn->response, RESPONSE_BUF_SIZE,
            S("200 OK"),
            S("application/json"),
            (const char *)stats_buf, stats_len);

    } else {
        /* 404 Not Found */
        lc_print_line(STDOUT, S("404"));
        conn->response_len = build_response(
            conn->response, RESPONSE_BUF_SIZE,
            S("404 Not Found"),
            S("text/html; charset=utf-8"),
            NOT_FOUND_BODY, sizeof(NOT_FOUND_BODY) - 1);
    }

    /* Submit async write for the response */
    lc_spinlock_acquire(&ring_lock);
    (void)lc_async_submit_write(ring, conn->fd,
                          conn->response, (uint32_t)conn->response_len,
                          (uint64_t)-1,
                          MAKE_TAG(slot, OP_WRITE));
    lc_async_flush(ring);
    lc_spinlock_release(&ring_lock);
}

/* ========================================================================
 * io_uring completion handler
 * ======================================================================== */

static void handle_completion(lc_async_ring *ring, const lc_async_result *result) {
    uint32_t slot = TAG_SLOT(result->tag);
    uint32_t op   = TAG_OP(result->tag);

    if (slot >= MAX_CONNECTIONS || !connections[slot].active) {
        return;  /* stale completion */
    }

    if (op == OP_READ) {
        /* Read completed — parse request and send response */
        handle_request(ring, (int32_t)slot, result->result);

    } else if (op == OP_WRITE) {
        /* Write completed — close the connection */
        lc_socket_close(connections[slot].fd);
        connection_free((int32_t)slot);
    }
}

/* ========================================================================
 * Accept thread — blocks on accept, submits async reads
 * ======================================================================== */

typedef struct {
    int32_t         server_fd;
    lc_async_ring  *ring;
} accept_thread_arg;

static int32_t accept_thread_func(void *arg) {
    accept_thread_arg *a = (accept_thread_arg *)arg;
    int32_t server_fd    = a->server_fd;
    lc_async_ring *ring  = a->ring;

    while (atomic_load(&server_running)) {
        /* Blocking accept — waits for a new connection */
        lc_socket_address client_addr;
        lc_result r = lc_socket_accept(server_fd, &client_addr);
        if (lc_is_err(r)) {
            continue;  /* accept error — try again */
        }
        int32_t client_fd = (int32_t)r.value;

        /* Allocate a connection slot */
        int32_t slot = connection_allocate(client_fd);
        if (slot < 0) {
            /* No free slots — reject */
            lc_print_line(STDOUT, S("[httpd] connection rejected: max reached"));
            lc_socket_close(client_fd);
            continue;
        }

        /* Submit async read for the HTTP request */
        lc_spinlock_acquire(&ring_lock);
        (void)lc_async_submit_read(ring, client_fd,
                             connections[slot].request, REQUEST_BUF_SIZE,
                             (uint64_t)-1,
                             MAKE_TAG(slot, OP_READ));
        lc_async_flush(ring);
        lc_spinlock_release(&ring_lock);
    }

    return 0;
}

/* ========================================================================
 * Main — setup and event loop
 * ======================================================================== */

int main(int argc, char **argv, char **envp) {
    (void)envp;

    /* Parse port from argv[1] or default to 8080 */
    uint16_t port = 8080;
    if (argc > 1) {
        uint16_t p = parse_port(argv[1]);
        if (p == 0) {
            lc_print_line(STDERR, S("httpd: invalid port number"));
            return 1;
        }
        port = p;
    }

    /* Initialize connection slots */
    for (int32_t i = 0; i < MAX_CONNECTIONS; i++) {
        connections[i].fd     = -1;
        connections[i].active = false;
    }

    /* Create io_uring ring */
    lc_result_ptr ring_r = lc_async_ring_create(RING_QUEUE_SIZE);
    if (lc_ptr_is_err(ring_r)) {
        lc_print_line(STDERR, S("httpd: failed to create io_uring ring"));
        return 1;
    }
    lc_async_ring *ring = ring_r.value;

    /* Create and bind server socket */
    lc_result r = lc_socket_listen_on(port, 128);
    if (lc_is_err(r)) {
        lc_print_line(STDERR, S("httpd: failed to bind server socket"));
        lc_async_ring_destroy(ring);
        return 1;
    }
    int32_t server_fd = (int32_t)r.value;

    /* Print startup message */
    lc_print_string(STDOUT, S("[httpd] lightc httpd listening on port "));
    lc_print_unsigned(STDOUT, port);
    lc_print_newline(STDOUT);
    lc_print_line(STDOUT, S("[httpd] endpoints: /  /hello  /stats"));

    /* Start the accept thread */
    accept_thread_arg thread_arg = { .server_fd = server_fd, .ring = ring };
    lc_thread accept_thread;
    if (lc_is_err(lc_thread_create(&accept_thread, accept_thread_func, &thread_arg))) {
        lc_print_line(STDERR, S("httpd: failed to create accept thread"));
        lc_socket_close(server_fd);
        lc_async_ring_destroy(ring);
        return 1;
    }

    /* Main event loop — process io_uring completions.
     *
     * We cannot call lc_async_wait while holding the spinlock, because
     * it blocks until a completion arrives — and the accept thread needs
     * the lock to submit the read that would produce that completion.
     *
     * Instead we split the wait into three steps:
     *   1. Lock -> flush pending submissions -> unlock
     *   2. io_uring_enter (blocking wait, no lock needed)
     *   3. Peek completions from the CQ ring
     *
     * The kernel handles concurrent access to the io_uring fd, and the
     * CQ ring is only read by this thread, so steps 2 and 3 are safe
     * without the lock. */
    while (atomic_load(&server_running)) {
        lc_async_result results[32];

        /* Step 1: flush any pending submissions under the lock */
        lc_spinlock_acquire(&ring_lock);
        lc_async_flush(ring);
        lc_spinlock_release(&ring_lock);

        /* Step 2: block until at least one completion is ready.
         * IORING_ENTER_GETEVENTS with min_complete=1. We pass the ring's
         * fd directly via the lc_kernel_io_ring_enter wrapper. We need
         * the ring fd — extract it from the ring struct.  Since
         * lc_async_ring is opaque, we cast to access the fd (it is the
         * first field).  This is specific to this demo. */
        int32_t ring_fd = *(int32_t *)ring;
        lc_kernel_io_ring_enter(ring_fd, 0, 1, 1 /* IORING_ENTER_GETEVENTS */);

        /* Step 3: reap completions (lock-free — only this thread reads CQ) */
        uint32_t n = lc_async_peek(ring, results, 32);

        /* Handle each completed operation */
        for (uint32_t i = 0; i < n; i++) {
            handle_completion(ring, &results[i]);
        }
    }

    /* Cleanup (unreachable in practice — server runs until killed) */
    lc_thread_join(&accept_thread);
    lc_socket_close(server_fd);
    lc_async_ring_destroy(ring);

    return 0;
}
