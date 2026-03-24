#include <lightio/lightio.h>
#include <lightc/print.h>
#include <lightc/string.h>
#include <lightc/format.h>
#include <lightc/syscall.h>
#include <lightc/socket.h>
#include <lightc/thread.h>
#include <lightc/time.h>
#include <stdatomic.h>

#define S(literal) literal, sizeof(literal) - 1

#define TEST_PORT 19876

/* ========================================================================
 * Echo handler — identical to the echo server, runs in a coroutine
 * ======================================================================== */

static void echo_handler(lio_stream *stream) {
    char buf[4096];

    while (true) {
        int32_t n = lio_read(stream, buf, sizeof(buf));
        if (n <= 0) break;

        int32_t w = lio_write(stream, buf, (uint32_t)n);
        if (w <= 0) break;
    }
}

/* ========================================================================
 * Server thread — runs lio_loop_run until stopped
 * ======================================================================== */

static int32_t server_thread(void *arg) {
    lio_loop *loop = (lio_loop *)arg;
    lio_loop_run(loop);
    return 0;
}

/* ========================================================================
 * Test: echo round-trip
 * ======================================================================== */

static bool test_echo(void) {
    lio_loop *loop = lio_loop_create();
    if (!loop) {
        lc_print_line(STDERR, S("  FAIL: could not create loop"));
        return false;
    }

    if (lc_is_err(lio_tcp_serve(loop, TEST_PORT, echo_handler))) {
        lc_print_line(STDERR, S("  FAIL: could not start server"));
        lio_loop_destroy(loop);
        return false;
    }

    /* Start server in a background thread */
    lc_thread t;
    if (lc_is_err(lc_thread_create(&t, server_thread, loop))) {
        lc_print_line(STDERR, S("  FAIL: could not create server thread"));
        lio_loop_destroy(loop);
        return false;
    }

    /* Give the server a moment to start accepting */
    lc_time_sleep_milliseconds(50);

    /* Connect as a client */
    lc_result r = lc_socket_connect_to(127, 0, 0, 1, TEST_PORT);
    if (lc_is_err(r)) {
        lc_print_line(STDERR, S("  FAIL: could not connect"));
        lio_loop_stop(loop);
        lc_thread_join(&t);
        lio_loop_destroy(loop);
        return false;
    }
    int32_t fd = (int32_t)r.value;

    /* Send test message */
    const char msg[] = "hello lightio";
    (void)lc_socket_send(fd, msg, sizeof(msg) - 1);

    /* Receive echo */
    char buf[64];
    lc_result rr = lc_socket_receive(fd, buf, sizeof(buf));
    lc_socket_close(fd);

    bool ok = (!lc_is_err(rr) && rr.value == (int64_t)(sizeof(msg) - 1) &&
               lc_string_equal(buf, (size_t)rr.value, msg, sizeof(msg) - 1));

    /* Stop the server */
    lio_loop_stop(loop);

    /*
     * The event loop might be blocked in io_uring_enter (waiting for CQEs).
     * Connect briefly to trigger an accept completion and unblock it.
     */
    r = lc_socket_connect_to(127, 0, 0, 1, TEST_PORT);
    if (!lc_is_err(r)) lc_socket_close((int32_t)r.value);

    lc_thread_join(&t);
    lio_loop_destroy(loop);

    return ok;
}

/* ========================================================================
 * Test: multiple concurrent clients
 * ======================================================================== */

static _Atomic(int32_t) clients_passed = 0;

static int32_t client_thread(void *arg) {
    uint16_t port = *(uint16_t *)arg;

    /* Small delay to stagger connections */
    lc_time_sleep_milliseconds(10);

    lc_result r = lc_socket_connect_to(127, 0, 0, 1, port);
    if (lc_is_err(r)) return 1;
    int32_t fd = (int32_t)r.value;

    const char msg[] = "concurrent test";
    (void)lc_socket_send(fd, msg, sizeof(msg) - 1);

    char buf[64];
    lc_result rr = lc_socket_receive(fd, buf, sizeof(buf));
    lc_socket_close(fd);

    if (!lc_is_err(rr) && rr.value == (int64_t)(sizeof(msg) - 1) &&
        lc_string_equal(buf, (size_t)rr.value, msg, sizeof(msg) - 1)) {
        atomic_fetch_add(&clients_passed, 1);
    }

    return 0;
}

static bool test_concurrent(void) {
    lio_loop *loop = lio_loop_create();
    if (!loop) return false;

    uint16_t port = TEST_PORT + 1;
    if (lc_is_err(lio_tcp_serve(loop, port, echo_handler))) {
        lio_loop_destroy(loop);
        return false;
    }

    lc_thread server;
    if (lc_is_err(lc_thread_create(&server, server_thread, loop))) {
        lio_loop_destroy(loop);
        return false;
    }

    lc_time_sleep_milliseconds(50);

    /* Launch 4 concurrent client threads */
    #define NUM_CLIENTS 4
    lc_thread clients[NUM_CLIENTS];
    atomic_store(&clients_passed, 0);

    for (int i = 0; i < NUM_CLIENTS; i++) {
        (void)lc_thread_create(&clients[i], client_thread, &port);
    }

    /* Wait for all clients */
    for (int i = 0; i < NUM_CLIENTS; i++) {
        lc_thread_join(&clients[i]);
    }

    bool ok = (atomic_load(&clients_passed) == NUM_CLIENTS);

    /* Stop server */
    lio_loop_stop(loop);
    lc_result wr = lc_socket_connect_to(127, 0, 0, 1, port);
    if (!lc_is_err(wr)) lc_socket_close((int32_t)wr.value);

    lc_thread_join(&server);
    lio_loop_destroy(loop);

    return ok;
}

/* ========================================================================
 * Test: multi-threaded server (4 worker threads)
 * ======================================================================== */

typedef struct {
    lio_loop *loop;
    uint16_t port;
} threaded_server_args;

static int32_t threaded_server_thread(void *arg) {
    threaded_server_args *a = (threaded_server_args *)arg;
    lio_loop_run_threaded(a->loop, 4);
    return 0;
}

static _Atomic(int32_t) mt_clients_passed = 0;

static int32_t mt_client_thread(void *arg) {
    uint16_t port = *(uint16_t *)arg;

    lc_time_sleep_milliseconds(10);

    lc_result r = lc_socket_connect_to(127, 0, 0, 1, port);
    if (lc_is_err(r)) return 1;
    int32_t fd = (int32_t)r.value;

    const char msg[] = "threaded test";
    (void)lc_socket_send(fd, msg, sizeof(msg) - 1);

    char buf[64];
    lc_result rr = lc_socket_receive(fd, buf, sizeof(buf));
    lc_socket_close(fd);

    if (!lc_is_err(rr) && rr.value == (int64_t)(sizeof(msg) - 1) &&
        lc_string_equal(buf, (size_t)rr.value, msg, sizeof(msg) - 1)) {
        atomic_fetch_add(&mt_clients_passed, 1);
    }

    return 0;
}

static bool test_threaded(void) {
    lio_loop *loop = lio_loop_create();
    if (!loop) return false;

    uint16_t port = TEST_PORT + 2;
    if (lc_is_err(lio_tcp_serve(loop, port, echo_handler))) {
        lio_loop_destroy(loop);
        return false;
    }

    /* Run server with 4 worker threads in a background thread */
    threaded_server_args sargs = { .loop = loop, .port = port };
    lc_thread server;
    if (lc_is_err(lc_thread_create(&server, threaded_server_thread, &sargs))) {
        lio_loop_destroy(loop);
        return false;
    }

    lc_time_sleep_milliseconds(100);

    /* Launch 8 concurrent client threads */
    #define MT_CLIENTS 8
    lc_thread clients[MT_CLIENTS];
    atomic_store(&mt_clients_passed, 0);

    for (int i = 0; i < MT_CLIENTS; i++) {
        (void)lc_thread_create(&clients[i], mt_client_thread, &port);
    }

    for (int i = 0; i < MT_CLIENTS; i++) {
        lc_thread_join(&clients[i]);
    }

    bool ok = (atomic_load(&mt_clients_passed) == MT_CLIENTS);

    /* Stop server */
    lio_loop_stop(loop);
    lc_time_sleep_milliseconds(200);

    lc_thread_join(&server);
    lio_loop_destroy(loop);

    return ok;
}

/* ========================================================================
 * Main — run all tests
 * ======================================================================== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    lc_print_line(STDOUT, S("lightio self-test"));
    lc_print_line(STDOUT, S("================="));

    int32_t passed = 0;
    int32_t failed = 0;

    /* Test 1: basic echo */
    lc_print_string(STDOUT, S("echo_roundtrip ... "));
    if (test_echo()) {
        lc_print_line(STDOUT, S("PASS"));
        passed++;
    } else {
        lc_print_line(STDOUT, S("FAIL"));
        failed++;
    }

    /* Test 2: concurrent clients */
    lc_print_string(STDOUT, S("concurrent_echo ... "));
    if (test_concurrent()) {
        lc_print_line(STDOUT, S("PASS"));
        passed++;
    } else {
        lc_print_line(STDOUT, S("FAIL"));
        failed++;
    }

    /* Test 3: multi-threaded server */
    lc_print_string(STDOUT, S("threaded_echo ... "));
    if (test_threaded()) {
        lc_print_line(STDOUT, S("PASS"));
        passed++;
    } else {
        lc_print_line(STDOUT, S("FAIL"));
        failed++;
    }

    /* Summary */
    lc_print_newline(STDOUT);
    char msg[128];
    lc_format fmt = lc_format_start(msg, sizeof(msg));
    lc_format_add_unsigned(&fmt, (uint64_t)passed);
    lc_format_add_text(&fmt, " passed, ");
    lc_format_add_unsigned(&fmt, (uint64_t)failed);
    lc_format_add_text(&fmt, " failed");
    lc_print_line(STDOUT, msg, lc_format_finish(&fmt));

    return failed > 0 ? 1 : 0;
}
