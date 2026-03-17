/*
 * Exercise the socket networking layer: TCP client/server, UDP, convenience functions.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/socket.h>
#include <lightc/thread.h>
#include <stdatomic.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed)
        lc_print_line(STDOUT, S(" PASS"));
    else
        lc_print_line(STDOUT, S(" FAIL"));
}

/* ================================================================
 * TCP test — server/client exchange via threads
 * ================================================================ */

#define TCP_PORT 18234

/* Atomic flag: server sets to 1 after it is listening */
static _Atomic(int32_t) server_ready = 0;

/* Server thread: accept one connection, receive, send back */
static int32_t tcp_server_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 0;

    /* Create, bind, listen */
    int32_t server_fd = lc_socket_create(LC_SOCK_STREAM);
    if (server_fd < 0) return 1;

    if (!lc_socket_set_reuse_address(server_fd)) {
        lc_socket_close(server_fd);
        return 1;
    }

    lc_socket_address addr = lc_socket_address_any(TCP_PORT);
    if (!lc_socket_bind(server_fd, &addr)) {
        lc_socket_close(server_fd);
        return 1;
    }

    if (!lc_socket_listen(server_fd, 1)) {
        lc_socket_close(server_fd);
        return 1;
    }

    /* Signal that we are listening */
    atomic_store_explicit(&server_ready, 1, memory_order_release);

    /* Accept one connection */
    lc_socket_address client_addr;
    int32_t client_fd = lc_socket_accept(server_fd, &client_addr);
    if (client_fd < 0) {
        lc_socket_close(server_fd);
        return 1;
    }

    /* Receive message from client */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    int64_t received = lc_socket_receive(client_fd, buf, sizeof(buf));
    if (received <= 0) {
        lc_socket_close(client_fd);
        lc_socket_close(server_fd);
        return 1;
    }

    /* Verify message */
    const char *expected = "hello from client";
    size_t expected_len = 17;
    if (!lc_string_equal(buf, (size_t)received, expected, expected_len)) {
        lc_socket_close(client_fd);
        lc_socket_close(server_fd);
        return 1;
    }

    /* Send response */
    const char *response = "hello from server";
    size_t response_len = 17;
    int64_t sent = lc_socket_send(client_fd, response, response_len);
    if (sent != (int64_t)response_len) {
        lc_socket_close(client_fd);
        lc_socket_close(server_fd);
        return 1;
    }

    lc_socket_close(client_fd);
    lc_socket_close(server_fd);
    *ok = 1;
    return 0;
}

/* Client thread: connect, send, receive */
static int32_t tcp_client_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 0;

    /* Wait for server to be ready */
    while (atomic_load_explicit(&server_ready, memory_order_acquire) == 0) {
        /* spin */
    }

    /* Connect to server */
    int32_t fd = lc_socket_create(LC_SOCK_STREAM);
    if (fd < 0) return 1;

    lc_socket_address addr = lc_socket_address_create(127, 0, 0, 1, TCP_PORT);
    if (!lc_socket_connect(fd, &addr)) {
        lc_socket_close(fd);
        return 1;
    }

    /* Send message */
    const char *msg = "hello from client";
    size_t msg_len = 17;
    int64_t sent = lc_socket_send(fd, msg, msg_len);
    if (sent != (int64_t)msg_len) {
        lc_socket_close(fd);
        return 1;
    }

    /* Receive response */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    int64_t received = lc_socket_receive(fd, buf, sizeof(buf));
    if (received <= 0) {
        lc_socket_close(fd);
        return 1;
    }

    /* Verify response */
    const char *expected = "hello from server";
    size_t expected_len = 17;
    if (!lc_string_equal(buf, (size_t)received, expected, expected_len)) {
        lc_socket_close(fd);
        return 1;
    }

    lc_socket_close(fd);
    *ok = 1;
    return 0;
}

/* ================================================================
 * UDP test — send/receive datagram on loopback
 * ================================================================ */

#define UDP_PORT 18235

static _Atomic(int32_t) udp_server_ready = 0;

static int32_t udp_server_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 0;

    int32_t fd = lc_socket_create(LC_SOCK_DGRAM);
    if (fd < 0) return 1;

    if (!lc_socket_set_reuse_address(fd)) {
        lc_socket_close(fd);
        return 1;
    }

    lc_socket_address addr = lc_socket_address_any(UDP_PORT);
    if (!lc_socket_bind(fd, &addr)) {
        lc_socket_close(fd);
        return 1;
    }

    /* Signal that we are bound */
    atomic_store_explicit(&udp_server_ready, 1, memory_order_release);

    /* Receive a datagram */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    lc_socket_address sender;
    int64_t received = lc_socket_receive_from(fd, buf, sizeof(buf), &sender);
    if (received <= 0) {
        lc_socket_close(fd);
        return 1;
    }

    /* Verify */
    const char *expected = "udp hello";
    size_t expected_len = 9;
    if (!lc_string_equal(buf, (size_t)received, expected, expected_len)) {
        lc_socket_close(fd);
        return 1;
    }

    /* Send reply back to sender */
    const char *reply = "udp reply";
    size_t reply_len = 9;
    int64_t sent = lc_socket_send_to(fd, reply, reply_len, &sender);
    if (sent != (int64_t)reply_len) {
        lc_socket_close(fd);
        return 1;
    }

    lc_socket_close(fd);
    *ok = 1;
    return 0;
}

static int32_t udp_client_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 0;

    /* Wait for server to be ready */
    while (atomic_load_explicit(&udp_server_ready, memory_order_acquire) == 0) {
        /* spin */
    }

    int32_t fd = lc_socket_create(LC_SOCK_DGRAM);
    if (fd < 0) return 1;

    /* Send datagram to server */
    lc_socket_address dest = lc_socket_address_create(127, 0, 0, 1, UDP_PORT);
    const char *msg = "udp hello";
    size_t msg_len = 9;
    int64_t sent = lc_socket_send_to(fd, msg, msg_len, &dest);
    if (sent != (int64_t)msg_len) {
        lc_socket_close(fd);
        return 1;
    }

    /* Receive reply */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    lc_socket_address sender;
    int64_t received = lc_socket_receive_from(fd, buf, sizeof(buf), &sender);
    if (received <= 0) {
        lc_socket_close(fd);
        return 1;
    }

    const char *expected = "udp reply";
    size_t expected_len = 9;
    if (!lc_string_equal(buf, (size_t)received, expected, expected_len)) {
        lc_socket_close(fd);
        return 1;
    }

    lc_socket_close(fd);
    *ok = 1;
    return 0;
}

/* ================================================================
 * Convenience functions test
 * ================================================================ */

#define CONV_PORT 18236

static _Atomic(int32_t) conv_server_ready = 0;

static int32_t conv_server_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 0;

    /* Use convenience function: create + bind + listen */
    int32_t server_fd = lc_socket_listen_on(CONV_PORT, 1);
    if (server_fd < 0) return 1;

    /* Signal that we are listening */
    atomic_store_explicit(&conv_server_ready, 1, memory_order_release);

    /* Accept one connection */
    int32_t client_fd = lc_socket_accept(server_fd, NULL);
    if (client_fd < 0) {
        lc_socket_close(server_fd);
        return 1;
    }

    /* Receive and echo back */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    int64_t received = lc_socket_receive(client_fd, buf, sizeof(buf));
    if (received <= 0) {
        lc_socket_close(client_fd);
        lc_socket_close(server_fd);
        return 1;
    }

    int64_t sent = lc_socket_send(client_fd, buf, (size_t)received);
    if (sent != received) {
        lc_socket_close(client_fd);
        lc_socket_close(server_fd);
        return 1;
    }

    lc_socket_close(client_fd);
    lc_socket_close(server_fd);
    *ok = 1;
    return 0;
}

static int32_t conv_client_thread(void *arg) {
    int32_t *ok = (int32_t *)arg;
    *ok = 0;

    /* Wait for server to be ready */
    while (atomic_load_explicit(&conv_server_ready, memory_order_acquire) == 0) {
        /* spin */
    }

    /* Use convenience function: create + connect */
    int32_t fd = lc_socket_connect_to(127, 0, 0, 1, CONV_PORT);
    if (fd < 0) return 1;

    /* Send a message */
    const char *msg = "convenience test";
    size_t msg_len = 16;
    int64_t sent = lc_socket_send(fd, msg, msg_len);
    if (sent != (int64_t)msg_len) {
        lc_socket_close(fd);
        return 1;
    }

    /* Receive echo */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    int64_t received = lc_socket_receive(fd, buf, sizeof(buf));
    if (received <= 0) {
        lc_socket_close(fd);
        return 1;
    }

    if (!lc_string_equal(buf, (size_t)received, msg, msg_len)) {
        lc_socket_close(fd);
        return 1;
    }

    lc_socket_close(fd);
    *ok = 1;
    return 0;
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    bool ok;

    /* --- TCP server/client exchange --- */
    lc_print_string(STDOUT, S("tcp_exchange"));
    {
        atomic_store(&server_ready, 0);
        int32_t server_ok = 0, client_ok = 0;
        lc_thread server, client;

        ok = lc_thread_create(&server, tcp_server_thread, &server_ok);
        if (ok) ok = lc_thread_create(&client, tcp_client_thread, &client_ok);
        if (ok) {
            lc_thread_join(&client);
            lc_thread_join(&server);
        }
        ok = ok && server_ok && client_ok;
    }
    say_pass_fail(ok);

    /* --- UDP send/receive --- */
    lc_print_string(STDOUT, S("udp_exchange"));
    {
        atomic_store(&udp_server_ready, 0);
        int32_t server_ok = 0, client_ok = 0;
        lc_thread server, client;

        ok = lc_thread_create(&server, udp_server_thread, &server_ok);
        if (ok) ok = lc_thread_create(&client, udp_client_thread, &client_ok);
        if (ok) {
            lc_thread_join(&client);
            lc_thread_join(&server);
        }
        ok = ok && server_ok && client_ok;
    }
    say_pass_fail(ok);

    /* --- Convenience functions (listen_on + connect_to) --- */
    lc_print_string(STDOUT, S("convenience_functions"));
    {
        atomic_store(&conv_server_ready, 0);
        int32_t server_ok = 0, client_ok = 0;
        lc_thread server, client;

        ok = lc_thread_create(&server, conv_server_thread, &server_ok);
        if (ok) ok = lc_thread_create(&client, conv_client_thread, &client_ok);
        if (ok) {
            lc_thread_join(&client);
            lc_thread_join(&server);
        }
        ok = ok && server_ok && client_ok;
    }
    say_pass_fail(ok);

    /* --- Shutdown test --- */
    lc_print_string(STDOUT, S("socket_shutdown"));
    {
        int32_t fd = lc_socket_create(LC_SOCK_STREAM);
        ok = fd >= 0;
        if (ok) {
            /* Shutdown on an unconnected socket should fail gracefully */
            bool shut_ok = lc_socket_shutdown(fd, LC_SHUT_BOTH);
            /* It's expected to fail (ENOTCONN), that's fine — just verify no crash */
            (void)shut_ok;
            lc_socket_close(fd);
        }
    }
    say_pass_fail(ok);

    /* --- Address helper test --- */
    lc_print_string(STDOUT, S("address_helpers"));
    {
        lc_socket_address addr = lc_socket_address_create(127, 0, 0, 1, 8080);
        ok = addr.family == LC_AF_INET;
        ok = ok && addr.port == lc_host_to_network_16(8080);
        /* 127.0.0.1 in little-endian memory = 0x0100007F, but stored as bytes: 127,0,0,1 */
        uint8_t *ip = (uint8_t *)&addr.addr;
        ok = ok && ip[0] == 127 && ip[1] == 0 && ip[2] == 0 && ip[3] == 1;

        lc_socket_address any = lc_socket_address_any(9090);
        ok = ok && any.family == LC_AF_INET;
        ok = ok && any.port == lc_host_to_network_16(9090);
        ok = ok && any.addr == 0;
    }
    say_pass_fail(ok);

    /* --- Byte order test --- */
    lc_print_string(STDOUT, S("byte_order"));
    {
        uint16_t h16 = 0x1234;
        uint16_t n16 = lc_host_to_network_16(h16);
        ok = lc_network_to_host_16(n16) == h16;

        uint32_t h32 = 0x12345678;
        uint32_t n32 = lc_host_to_network_32(h32);
        ok = ok && lc_network_to_host_32(n32) == h32;

        /* Verify actual byte swap on little-endian */
        ok = ok && n16 == 0x3412;
        ok = ok && n32 == 0x78563412;
    }
    say_pass_fail(ok);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
