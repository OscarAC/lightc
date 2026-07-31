/*
 * test_lightio.c — tests for the lightio async event loop.
 *
 * lightio needs io_uring (kernel 5.1+); if it is unavailable the whole suite
 * skips, mirroring test_async. These exercise the accept -> coroutine ->
 * lio_read/lio_write -> completion -> slot-cleanup path end to end (previously
 * untested), which is exactly the path the H4 submission-handling and
 * slot-rollback fixes touch.
 */

#include "test.h"
#include <lightio/lightio.h>
#include <lightc/async.h>
#include <lightc/socket.h>
#include <lightc/string.h>
#include <lightc/thread.h>
#include <lightc/time.h>
#include <stdatomic.h>

/* ===== Echo handler + server thread ===== */

static void echo_handler(lio_stream *stream) {
    char buf[4096];
    for (;;) {
        int32_t n = lio_read(stream, buf, sizeof(buf));
        if (n <= 0) break;
        int32_t w = lio_write(stream, buf, (uint32_t)n);
        if (w <= 0) break;
    }
}

static int32_t server_thread(void *arg) {
    lio_loop_run((lio_loop *)arg);
    return 0;
}

/* Stop a running loop and join its thread. The loop may be parked in
 * io_uring_enter, so a throwaway connection triggers an accept completion to
 * wake it (same technique the lightio examples use). */
static void stop_and_join(lio_loop *loop, lc_thread *t, uint16_t port) {
    lio_loop_stop(loop);
    lc_result r = lc_socket_connect_to(127, 0, 0, 1, port);
    if (!lc_is_err(r)) lc_socket_close((int32_t)r.value);
    lc_thread_join(t);
}

/* One request/response over a fresh connection. Returns true on a correct echo. */
static bool echo_once(uint16_t port, const char *msg, size_t len) {
    lc_result r = lc_socket_connect_to(127, 0, 0, 1, port);
    if (lc_is_err(r)) return false;
    int32_t fd = (int32_t)r.value;

    if (lc_is_err(lc_socket_send(fd, msg, len))) { lc_socket_close(fd); return false; }

    char buf[4096];
    lc_result rr = lc_socket_receive(fd, buf, sizeof(buf));
    lc_socket_close(fd);

    return !lc_is_err(rr) && rr.value == (int64_t)len &&
           lc_string_equal(buf, (size_t)rr.value, msg, len);
}

/* ===== Test: single echo round-trip ===== */

static void test_lightio_echo_roundtrip(void) {
    const uint16_t port = 19910;

    lio_loop *loop = lio_loop_create();
    TEST_ASSERT_NOT_NULL(loop);
    TEST_ASSERT_OK(lio_tcp_serve(loop, port, echo_handler));

    lc_thread t;
    TEST_ASSERT_OK(lc_thread_create(&t, server_thread, loop));
    lc_time_sleep_milliseconds(50);  /* let the server start accepting */

    const char msg[] = "hello lightio";
    TEST_ASSERT(echo_once(port, msg, sizeof(msg) - 1));

    stop_and_join(loop, &t, port);
    lio_loop_destroy(loop);
}

/* ===== Test: many sequential connections recycle slots cleanly =====
 *
 * Each connection runs a handler coroutine to completion, which must free its
 * slot (state -> FREE, active_count decremented, stack unmapped). If slots
 * leaked instead of recycling, the server would run out after LIO_MAX_SLOTS and
 * stop echoing; 200 sequential round-trips on a single loop exercise that
 * recycling well past any small-slot boundary.
 */
static void test_lightio_sequential_reuse(void) {
    const uint16_t port = 19911;

    lio_loop *loop = lio_loop_create();
    TEST_ASSERT_NOT_NULL(loop);
    TEST_ASSERT_OK(lio_tcp_serve(loop, port, echo_handler));

    lc_thread t;
    TEST_ASSERT_OK(lc_thread_create(&t, server_thread, loop));
    lc_time_sleep_milliseconds(50);

    char msg[32];
    for (int i = 0; i < 200; i++) {
        /* Vary the payload so a stale buffer can't masquerade as a pass. */
        int32_t len = 0;
        msg[len++] = 'r'; msg[len++] = 'q';
        msg[len++] = (char)('0' + (i / 100) % 10);
        msg[len++] = (char)('0' + (i / 10) % 10);
        msg[len++] = (char)('0' + i % 10);
        TEST_ASSERT(echo_once(port, msg, (size_t)len));
    }

    stop_and_join(loop, &t, port);
    lio_loop_destroy(loop);
}

/* ===== Test: multiple concurrent clients ===== */

typedef struct { uint16_t port; _Atomic(int32_t) *passed; } client_args;

static int32_t client_thread(void *arg) {
    client_args *a = (client_args *)arg;
    lc_time_sleep_milliseconds(10);  /* stagger connects */
    const char msg[] = "concurrent";
    if (echo_once(a->port, msg, sizeof(msg) - 1)) {
        atomic_fetch_add(a->passed, 1);
    }
    return 0;
}

static void test_lightio_concurrent_clients(void) {
    const uint16_t port = 19912;

    lio_loop *loop = lio_loop_create();
    TEST_ASSERT_NOT_NULL(loop);
    TEST_ASSERT_OK(lio_tcp_serve(loop, port, echo_handler));

    lc_thread server;
    TEST_ASSERT_OK(lc_thread_create(&server, server_thread, loop));
    lc_time_sleep_milliseconds(50);

    #define LIO_TEST_CLIENTS 8
    _Atomic(int32_t) passed = 0;
    client_args ca = { port, &passed };
    lc_thread clients[LIO_TEST_CLIENTS];
    for (int i = 0; i < LIO_TEST_CLIENTS; i++) {
        TEST_ASSERT_OK(lc_thread_create(&clients[i], client_thread, &ca));
    }
    for (int i = 0; i < LIO_TEST_CLIENTS; i++) {
        lc_thread_join(&clients[i]);
    }

    TEST_ASSERT_EQ(atomic_load(&passed), LIO_TEST_CLIENTS);

    stop_and_join(loop, &server, port);
    lio_loop_destroy(loop);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Skip the whole suite if io_uring is unavailable (kernel < 5.1). */
    {
        lc_result_ptr probe = lc_async_ring_create(4);
        if (lc_ptr_is_err(probe)) {
            _test_print_text("SKIP: io_uring not available (kernel 5.1+ required)\n");
            _test_print_text("\n========================================\n");
            _test_print_text("Tests run:    0\nTests passed: 0\nTests failed: 0\n");
            _test_print_text("========================================\nRESULT: PASS\n");
            return 0;
        }
        lc_async_ring_destroy(probe.value);
    }

    TEST_RUN(test_lightio_echo_roundtrip);
    TEST_RUN(test_lightio_sequential_reuse);
    TEST_RUN(test_lightio_concurrent_clients);

    return test_main();
}
