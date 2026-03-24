/*
 * test_socket.c — tests for lightc socket networking.
 */

#include "test.h"
#include <lightc/socket.h>
#include <lightc/thread.h>
#include <lightc/string.h>
#include <lightc/time.h>
#include <stdatomic.h>

/* ===== lc_socket_address_create ===== */

static void test_socket_address_create(void) {
    lc_socket_address addr = lc_socket_address_create(127, 0, 0, 1, 8080);

    TEST_ASSERT_EQ(addr.family, (uint16_t)LC_AF_INET);
    TEST_ASSERT_EQ(addr.port, lc_host_to_network_16(8080));

    /* 127.0.0.1 in network byte order = 0x7F000001 big-endian */
    TEST_ASSERT_EQ(addr.addr, lc_host_to_network_32(0x7F000001));
}

/* ===== lc_socket_address_any ===== */

static void test_socket_address_any(void) {
    lc_socket_address addr = lc_socket_address_any(9090);

    TEST_ASSERT_EQ(addr.family, (uint16_t)LC_AF_INET);
    TEST_ASSERT_EQ(addr.port, lc_host_to_network_16(9090));
    TEST_ASSERT_EQ(addr.addr, lc_host_to_network_32(LC_INADDR_ANY));
}

/* ===== Byte order round-trip ===== */

static void test_byte_order(void) {
    /* 16-bit round-trip */
    uint16_t val16 = 0x1234;
    uint16_t net16 = lc_host_to_network_16(val16);
    uint16_t host16 = lc_network_to_host_16(net16);
    TEST_ASSERT_EQ(host16, val16);

    /* 32-bit round-trip */
    uint32_t val32 = 0xDEADBEEF;
    uint32_t net32 = lc_host_to_network_32(val32);
    uint32_t host32 = lc_network_to_host_32(net32);
    TEST_ASSERT_EQ(host32, val32);

    /* Network byte order should be big-endian */
    uint16_t test16 = lc_host_to_network_16(0x0102);
    uint8_t *bytes16 = (uint8_t *)&test16;
    TEST_ASSERT_EQ(bytes16[0], (uint8_t)0x01);
    TEST_ASSERT_EQ(bytes16[1], (uint8_t)0x02);
}

/* ===== Socket create and close ===== */

static void test_socket_create_close(void) {
    /* TCP socket */
    lc_result r = lc_socket_create(LC_SOCK_STREAM);
    TEST_ASSERT_OK(r);
    TEST_ASSERT(r.value >= 0);
    lc_socket_close((int32_t)r.value);

    /* UDP socket */
    lc_result r2 = lc_socket_create(LC_SOCK_DGRAM);
    TEST_ASSERT_OK(r2);
    TEST_ASSERT(r2.value >= 0);
    lc_socket_close((int32_t)r2.value);
}

/* ===== TCP exchange with background thread ===== */

#define TCP_TEST_PORT 19001

static _Atomic(int32_t) tcp_server_ready;
static _Atomic(int32_t) tcp_server_ok;

static int32_t tcp_server_thread(void *arg) {
    (void)arg;

    /* Create server socket */
    lc_result sr = lc_socket_create(LC_SOCK_STREAM);
    if (lc_is_err(sr)) { atomic_store(&tcp_server_ok, 0); return 1; }
    int32_t server_fd = (int32_t)sr.value;

    lc_socket_set_reuse_address(server_fd);

    lc_socket_address addr = lc_socket_address_any(TCP_TEST_PORT);
    lc_result br = lc_socket_bind(server_fd, &addr);
    if (lc_is_err(br)) { lc_socket_close(server_fd); atomic_store(&tcp_server_ok, 0); return 1; }

    lc_result lr = lc_socket_listen(server_fd, 1);
    if (lc_is_err(lr)) { lc_socket_close(server_fd); atomic_store(&tcp_server_ok, 0); return 1; }

    /* Signal that server is ready */
    atomic_store(&tcp_server_ready, 1);

    /* Accept one connection */
    lc_socket_address client_addr;
    lc_result ar = lc_socket_accept(server_fd, &client_addr);
    if (lc_is_err(ar)) { lc_socket_close(server_fd); atomic_store(&tcp_server_ok, 0); return 1; }
    int32_t client_fd = (int32_t)ar.value;

    /* Read data */
    char buf[64];
    lc_result rr = lc_socket_receive(client_fd, buf, sizeof(buf));
    if (lc_is_err(rr) || rr.value == 0) {
        lc_socket_close(client_fd);
        lc_socket_close(server_fd);
        atomic_store(&tcp_server_ok, 0);
        return 1;
    }

    /* Echo it back */
    lc_socket_send(client_fd, buf, (size_t)rr.value);

    lc_socket_close(client_fd);
    lc_socket_close(server_fd);
    atomic_store(&tcp_server_ok, 1);
    return 0;
}

static void test_socket_tcp_exchange(void) {
    atomic_store(&tcp_server_ready, 0);
    atomic_store(&tcp_server_ok, 0);

    /* Start server thread */
    lc_thread t;
    lc_result tr = lc_thread_create(&t, tcp_server_thread, NULL);
    TEST_ASSERT_OK(tr);

    /* Wait for server to be ready */
    while (atomic_load(&tcp_server_ready) == 0) {
        lc_time_sleep_microseconds(100);
    }

    /* Connect as client */
    lc_result cr = lc_socket_connect_to(127, 0, 0, 1, TCP_TEST_PORT);
    TEST_ASSERT_OK(cr);
    int32_t client_fd = (int32_t)cr.value;

    /* Send data */
    const char *msg = "tcp test";
    size_t msg_len = lc_string_length(msg);
    lc_result sr = lc_socket_send(client_fd, msg, msg_len);
    TEST_ASSERT_OK(sr);
    TEST_ASSERT_EQ(sr.value, (int64_t)msg_len);

    /* Receive echo */
    char buf[64];
    lc_result rr = lc_socket_receive(client_fd, buf, sizeof(buf));
    TEST_ASSERT_OK(rr);
    TEST_ASSERT_EQ(rr.value, (int64_t)msg_len);
    TEST_ASSERT_STR_EQ(buf, (size_t)rr.value, msg, msg_len);

    lc_socket_close(client_fd);
    lc_thread_join(&t);

    TEST_ASSERT_EQ(atomic_load(&tcp_server_ok), 1);
}

/* ===== UDP exchange with background thread ===== */

#define UDP_TEST_PORT 19002

static _Atomic(int32_t) udp_server_ready;
static _Atomic(int32_t) udp_server_ok;

static int32_t udp_server_thread(void *arg) {
    (void)arg;

    /* Create UDP socket */
    lc_result sr = lc_socket_create(LC_SOCK_DGRAM);
    if (lc_is_err(sr)) { atomic_store(&udp_server_ok, 0); return 1; }
    int32_t server_fd = (int32_t)sr.value;

    lc_socket_set_reuse_address(server_fd);

    lc_socket_address addr = lc_socket_address_any(UDP_TEST_PORT);
    lc_result br = lc_socket_bind(server_fd, &addr);
    if (lc_is_err(br)) { lc_socket_close(server_fd); atomic_store(&udp_server_ok, 0); return 1; }

    /* Signal that server is ready */
    atomic_store(&udp_server_ready, 1);

    /* Receive one datagram */
    char buf[64];
    lc_socket_address sender;
    lc_result rr = lc_socket_receive_from(server_fd, buf, sizeof(buf), &sender);
    if (lc_is_err(rr) || rr.value == 0) {
        lc_socket_close(server_fd);
        atomic_store(&udp_server_ok, 0);
        return 1;
    }

    /* Echo it back to sender */
    lc_socket_send_to(server_fd, buf, (size_t)rr.value, &sender);

    lc_socket_close(server_fd);
    atomic_store(&udp_server_ok, 1);
    return 0;
}

static void test_socket_udp_exchange(void) {
    atomic_store(&udp_server_ready, 0);
    atomic_store(&udp_server_ok, 0);

    /* Start server thread */
    lc_thread t;
    lc_result tr = lc_thread_create(&t, udp_server_thread, NULL);
    TEST_ASSERT_OK(tr);

    /* Wait for server to be ready */
    while (atomic_load(&udp_server_ready) == 0) {
        lc_time_sleep_microseconds(100);
    }

    /* Create client UDP socket */
    lc_result cr = lc_socket_create(LC_SOCK_DGRAM);
    TEST_ASSERT_OK(cr);
    int32_t client_fd = (int32_t)cr.value;

    /* Send datagram to server */
    lc_socket_address dest = lc_socket_address_create(127, 0, 0, 1, UDP_TEST_PORT);
    const char *msg = "udp test";
    size_t msg_len = lc_string_length(msg);
    lc_result sr = lc_socket_send_to(client_fd, msg, msg_len, &dest);
    TEST_ASSERT_OK(sr);
    TEST_ASSERT_EQ(sr.value, (int64_t)msg_len);

    /* Receive echo */
    char buf[64];
    lc_socket_address sender;
    lc_result rr = lc_socket_receive_from(client_fd, buf, sizeof(buf), &sender);
    TEST_ASSERT_OK(rr);
    TEST_ASSERT_EQ(rr.value, (int64_t)msg_len);
    TEST_ASSERT_STR_EQ(buf, (size_t)rr.value, msg, msg_len);

    lc_socket_close(client_fd);
    lc_thread_join(&t);

    TEST_ASSERT_EQ(atomic_load(&udp_server_ok), 1);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* address create */
    TEST_RUN(test_socket_address_create);

    /* address any */
    TEST_RUN(test_socket_address_any);

    /* byte order round-trip */
    TEST_RUN(test_byte_order);

    /* socket create/close */
    TEST_RUN(test_socket_create_close);

    /* TCP echo exchange */
    TEST_RUN(test_socket_tcp_exchange);

    /* UDP echo exchange */
    TEST_RUN(test_socket_udp_exchange);

    return test_main();
}
