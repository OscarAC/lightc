#ifndef LIGHTC_SOCKET_H
#define LIGHTC_SOCKET_H

#include "types.h"

/*
 * Socket networking — built on raw syscalls, no libc.
 *
 * Supports TCP (stream) and UDP (datagram) over IPv4.
 */

/* Address families */
#define LC_AF_INET      2

/* Socket types */
#define LC_SOCK_STREAM  1    /* TCP */
#define LC_SOCK_DGRAM   2    /* UDP */

/* Protocol */
#define LC_IPPROTO_TCP  6
#define LC_IPPROTO_UDP  17

/* Shutdown modes */
#define LC_SHUT_READ    0
#define LC_SHUT_WRITE   1
#define LC_SHUT_BOTH    2

/* Socket options */
#define LC_SOL_SOCKET       1
#define LC_SO_REUSEADDR     2
#define LC_SO_REUSEPORT     15

/* Special addresses */
#define LC_INADDR_ANY       0x00000000
#define LC_INADDR_LOOPBACK  0x7F000001

/* IPv4 address (network byte order) */
typedef struct {
    uint16_t family;      /* LC_AF_INET */
    uint16_t port;        /* port in network byte order */
    uint32_t addr;        /* IPv4 address in network byte order */
    uint8_t  _pad[8];     /* padding to 16 bytes (sockaddr_in size) */
} lc_socket_address;

/* --- Byte order helpers --- */

[[gnu::const]]
static inline uint16_t lc_host_to_network_16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

[[gnu::const]]
static inline uint32_t lc_host_to_network_32(uint32_t value) {
    return ((value >> 24) & 0xFF) |
           ((value >> 8)  & 0xFF00) |
           ((value << 8)  & 0xFF0000) |
           ((value << 24) & 0xFF000000);
}

[[gnu::const]]
static inline uint16_t lc_network_to_host_16(uint16_t value) {
    return lc_host_to_network_16(value);
}

[[gnu::const]]
static inline uint32_t lc_network_to_host_32(uint32_t value) {
    return lc_host_to_network_32(value);
}

/* --- Address helpers --- */

/* Create an address from IPv4 components and port.
 * Example: lc_socket_address_create(127, 0, 0, 1, 8080) */
lc_socket_address lc_socket_address_create(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);

/* Create an address for INADDR_ANY with the given port. */
lc_socket_address lc_socket_address_any(uint16_t port);

/* --- Socket operations --- */

/* Create a socket. value = fd on success. */
[[nodiscard]] lc_result lc_socket_create(int32_t type);

/* Close a socket. */
void lc_socket_close(int32_t socket_fd);

/* Set socket option to reuse address. */
[[nodiscard]] lc_result lc_socket_set_reuse_address(int32_t socket_fd);

/* Bind socket to an address. */
[[nodiscard]] lc_result lc_socket_bind(int32_t socket_fd, const lc_socket_address *addr);

/* Start listening for connections (TCP). */
[[nodiscard]] lc_result lc_socket_listen(int32_t socket_fd, int32_t backlog);

/* Accept a connection (TCP). value = new fd, fills client_addr if non-NULL. */
[[nodiscard]] lc_result lc_socket_accept(int32_t socket_fd, lc_socket_address *client_addr);

/* Connect to a remote address (TCP client). */
[[nodiscard]] lc_result lc_socket_connect(int32_t socket_fd, const lc_socket_address *addr);

/* Send data on a connected socket. value = bytes sent. */
[[nodiscard]] lc_result lc_socket_send(int32_t socket_fd, const void *buf, size_t count);

/* Receive data from a connected socket. value = bytes received (0 on close). */
[[nodiscard]] lc_result lc_socket_receive(int32_t socket_fd, void *buf, size_t count);

/* Send data to a specific address (UDP). value = bytes sent. */
[[nodiscard]] lc_result lc_socket_send_to(int32_t socket_fd, const void *buf, size_t count,
                          const lc_socket_address *dest);

/* Receive data and get sender's address (UDP). value = bytes received. */
[[nodiscard]] lc_result lc_socket_receive_from(int32_t socket_fd, void *buf, size_t count,
                               lc_socket_address *sender);

/* Shutdown part of a connection. */
[[nodiscard]] lc_result lc_socket_shutdown(int32_t socket_fd, int32_t how);

/* --- Convenience: TCP server --- */

/* Create, bind, listen in one call. value = socket fd. */
[[nodiscard]] lc_result lc_socket_listen_on(uint16_t port, int32_t backlog);

/* --- Convenience: TCP client --- */

/* Create and connect in one call. value = socket fd. */
[[nodiscard]] lc_result lc_socket_connect_to(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);

/* ------------------------------------------------------------------ */
/* IPv6                                                               */
/* ------------------------------------------------------------------ */

#define LC_AF_INET6 10

/* IPv6 address — matches struct sockaddr_in6 layout */
typedef struct {
    uint16_t family;     /* LC_AF_INET6 */
    uint16_t port;       /* network byte order */
    uint32_t flowinfo;
    uint8_t  addr[16];   /* 128-bit IPv6 address */
    uint32_t scope_id;
} lc_socket_address6;

/* Create IPv6 sockets */
[[nodiscard]] lc_result lc_socket_tcp6(void);
[[nodiscard]] lc_result lc_socket_udp6(void);

/* Create an IPv6 address from components */
lc_socket_address6 lc_socket_address6_create(const uint8_t addr[16], uint16_t port);

/* Create loopback (::1) address */
lc_socket_address6 lc_socket_address6_loopback(uint16_t port);

/* Create any-address (::) */
lc_socket_address6 lc_socket_address6_any(uint16_t port);

/* IPv6 type-safe wrappers */
[[nodiscard]] lc_result lc_socket_bind6(int32_t fd, const lc_socket_address6 *addr);
[[nodiscard]] lc_result lc_socket_listen6(int32_t fd, int32_t backlog);
[[nodiscard]] lc_result lc_socket_accept6(int32_t fd, lc_socket_address6 *client_addr);
[[nodiscard]] lc_result lc_socket_connect6(int32_t fd, const lc_socket_address6 *addr);

#endif /* LIGHTC_SOCKET_H */
