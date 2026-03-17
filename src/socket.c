#include <lightc/socket.h>
#include <lightc/syscall.h>
#include <lightc/string.h>

_Static_assert(sizeof(lc_socket_address) == 16, "lc_socket_address must be 16 bytes (sockaddr_in)");

/* --- Address helpers --- */

lc_socket_address lc_socket_address_create(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
    lc_socket_address addr;
    lc_bytes_fill(&addr, 0, sizeof(addr));
    addr.family = LC_AF_INET;
    addr.port   = lc_host_to_network_16(port);
    addr.addr   = (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
    return addr;
}

lc_socket_address lc_socket_address_any(uint16_t port) {
    lc_socket_address addr;
    lc_bytes_fill(&addr, 0, sizeof(addr));
    addr.family = LC_AF_INET;
    addr.port   = lc_host_to_network_16(port);
    addr.addr   = lc_host_to_network_32(LC_INADDR_ANY);
    return addr;
}

/* --- Socket operations --- */

int32_t lc_socket_create(int32_t type) {
    lc_sysret ret = lc_syscall3(SYS_socket, LC_AF_INET, type, 0);
    return (int32_t)ret;
}

void lc_socket_close(int32_t socket_fd) {
    lc_kernel_close_file(socket_fd);
}

bool lc_socket_set_reuse_address(int32_t socket_fd) {
    int32_t val = 1;
    lc_sysret ret = lc_syscall5(SYS_setsockopt, socket_fd, LC_SOL_SOCKET,
                                LC_SO_REUSEADDR, (int64_t)&val, sizeof(val));
    return ret >= 0;
}

bool lc_socket_bind(int32_t socket_fd, const lc_socket_address *addr) {
    lc_sysret ret = lc_syscall3(SYS_bind, socket_fd, (int64_t)addr, sizeof(lc_socket_address));
    return ret >= 0;
}

bool lc_socket_listen(int32_t socket_fd, int32_t backlog) {
    lc_sysret ret = lc_syscall2(SYS_listen, socket_fd, backlog);
    return ret >= 0;
}

int32_t lc_socket_accept(int32_t socket_fd, lc_socket_address *client_addr) {
    uint32_t addrlen = sizeof(lc_socket_address);
    lc_sysret ret = lc_syscall4(SYS_accept4, socket_fd,
                                (int64_t)client_addr, (int64_t)&addrlen, 0);
    return (int32_t)ret;
}

bool lc_socket_connect(int32_t socket_fd, const lc_socket_address *addr) {
    lc_sysret ret = lc_syscall3(SYS_connect, socket_fd, (int64_t)addr, sizeof(lc_socket_address));
    return ret >= 0;
}

int64_t lc_socket_send(int32_t socket_fd, const void *buf, size_t count) {
    return lc_syscall6(SYS_sendto, socket_fd, (int64_t)buf, (int64_t)count,
                       0, 0, 0);
}

int64_t lc_socket_receive(int32_t socket_fd, void *buf, size_t count) {
    return lc_syscall6(SYS_recvfrom, socket_fd, (int64_t)buf, (int64_t)count,
                       0, 0, 0);
}

int64_t lc_socket_send_to(int32_t socket_fd, const void *buf, size_t count,
                          const lc_socket_address *dest) {
    return lc_syscall6(SYS_sendto, socket_fd, (int64_t)buf, (int64_t)count,
                       0, (int64_t)dest, sizeof(lc_socket_address));
}

int64_t lc_socket_receive_from(int32_t socket_fd, void *buf, size_t count,
                               lc_socket_address *sender) {
    uint32_t addrlen = sizeof(lc_socket_address);
    return lc_syscall6(SYS_recvfrom, socket_fd, (int64_t)buf, (int64_t)count,
                       0, (int64_t)sender, (int64_t)&addrlen);
}

bool lc_socket_shutdown(int32_t socket_fd, int32_t how) {
    lc_sysret ret = lc_syscall2(SYS_shutdown, socket_fd, how);
    return ret >= 0;
}

/* --- Convenience functions --- */

int32_t lc_socket_listen_on(uint16_t port, int32_t backlog) {
    int32_t fd = lc_socket_create(LC_SOCK_STREAM);
    if (fd < 0) return fd;

    if (!lc_socket_set_reuse_address(fd)) {
        lc_socket_close(fd);
        return -1;
    }

    lc_socket_address addr = lc_socket_address_any(port);
    if (!lc_socket_bind(fd, &addr)) {
        lc_socket_close(fd);
        return -1;
    }

    if (!lc_socket_listen(fd, backlog)) {
        lc_socket_close(fd);
        return -1;
    }

    return fd;
}

int32_t lc_socket_connect_to(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
    int32_t fd = lc_socket_create(LC_SOCK_STREAM);
    if (fd < 0) return fd;

    lc_socket_address addr = lc_socket_address_create(a, b, c, d, port);
    if (!lc_socket_connect(fd, &addr)) {
        lc_socket_close(fd);
        return -1;
    }

    return fd;
}

/* ------------------------------------------------------------------ */
/* IPv6                                                               */
/* ------------------------------------------------------------------ */

_Static_assert(sizeof(lc_socket_address6) == 28, "lc_socket_address6 must be 28 bytes (sockaddr_in6)");

int32_t lc_socket_tcp6(void) {
    lc_sysret ret = lc_syscall3(SYS_socket, LC_AF_INET6, LC_SOCK_STREAM, 0);
    return (int32_t)ret;
}

int32_t lc_socket_udp6(void) {
    lc_sysret ret = lc_syscall3(SYS_socket, LC_AF_INET6, LC_SOCK_DGRAM, 0);
    return (int32_t)ret;
}

lc_socket_address6 lc_socket_address6_create(const uint8_t addr[16], uint16_t port) {
    lc_socket_address6 sa;
    lc_bytes_fill(&sa, 0, sizeof(sa));
    sa.family = LC_AF_INET6;
    sa.port   = lc_host_to_network_16(port);
    lc_bytes_copy(sa.addr, addr, 16);
    return sa;
}

lc_socket_address6 lc_socket_address6_loopback(uint16_t port) {
    uint8_t addr[16] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1};
    return lc_socket_address6_create(addr, port);
}

lc_socket_address6 lc_socket_address6_any(uint16_t port) {
    uint8_t addr[16] = {0};
    return lc_socket_address6_create(addr, port);
}

bool lc_socket_bind6(int32_t fd, const lc_socket_address6 *addr) {
    lc_sysret ret = lc_syscall3(SYS_bind, fd, (int64_t)addr, sizeof(lc_socket_address6));
    return ret >= 0;
}

bool lc_socket_listen6(int32_t fd, int32_t backlog) {
    lc_sysret ret = lc_syscall2(SYS_listen, fd, backlog);
    return ret >= 0;
}

int32_t lc_socket_accept6(int32_t fd, lc_socket_address6 *client_addr) {
    uint32_t addrlen = sizeof(lc_socket_address6);
    lc_sysret ret = lc_syscall4(SYS_accept4, fd,
                                (int64_t)client_addr, (int64_t)&addrlen, 0);
    return (int32_t)ret;
}

bool lc_socket_connect6(int32_t fd, const lc_socket_address6 *addr) {
    lc_sysret ret = lc_syscall3(SYS_connect, fd, (int64_t)addr, sizeof(lc_socket_address6));
    return ret >= 0;
}
