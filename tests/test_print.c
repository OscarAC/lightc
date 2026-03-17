/*
 * test_print.c — tests for lightc unbuffered print functions.
 *
 * Strategy: create a pipe and pass the write fd directly to
 * lc_print_* functions (they all take fd as first argument),
 * then read from the read end to verify output.
 */

#include "test.h"
#include <lightc/print.h>
#include <lightc/syscall.h>
#include <lightc/string.h>

/* Helper: create a pipe pair. */
static int pipe_create(int32_t *read_fd, int32_t *write_fd) {
    int32_t fds[2];
    lc_sysret ret = lc_kernel_create_pipe(fds, 0);
    if (ret < 0) return -1;
    *read_fd  = fds[0];
    *write_fd = fds[1];
    return 0;
}

/* Helper: read all available bytes from fd. */
static size_t raw_read(int32_t fd, char *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        lc_sysret ret = lc_kernel_read_bytes(fd, buf + total, count - total);
        if (ret <= 0) break;
        total += (size_t)ret;
    }
    return total;
}

/* ===== lc_print_string ===== */

static void test_print_string(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_string(wfd, "hello", 5);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 5);
    TEST_ASSERT_STR_EQ(buf, 5, "hello", 5);

    lc_kernel_close_file(rfd);
}

/* ===== lc_print_line ===== */

static void test_print_line(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_line(wfd, "hello", 5);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 6);
    TEST_ASSERT_STR_EQ(buf, 6, "hello\n", 6);

    lc_kernel_close_file(rfd);
}

/* ===== lc_print_signed ===== */

static void test_print_signed_positive(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_signed(wfd, 42);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "42", 2);

    lc_kernel_close_file(rfd);
}

static void test_print_signed_negative(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_signed(wfd, -123);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "-123", 4);

    lc_kernel_close_file(rfd);
}

static void test_print_signed_zero(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_signed(wfd, 0);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "0", 1);

    lc_kernel_close_file(rfd);
}

/* ===== lc_print_unsigned ===== */

static void test_print_unsigned_zero(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_unsigned(wfd, 0);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "0", 1);

    lc_kernel_close_file(rfd);
}

static void test_print_unsigned_large(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_unsigned(wfd, 18446744073709551615ULL);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "18446744073709551615", 20);

    lc_kernel_close_file(rfd);
}

static void test_print_unsigned_small(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_unsigned(wfd, 255);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "255", 3);

    lc_kernel_close_file(rfd);
}

/* ===== lc_print_hex ===== */

static void test_print_hex_zero(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_hex(wfd, 0);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "0x0", 3);

    lc_kernel_close_file(rfd);
}

static void test_print_hex_value(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_hex(wfd, 0xDEAD);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "0xdead", 6);

    lc_kernel_close_file(rfd);
}

/* ===== lc_print_char ===== */

static void test_print_char(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_char(wfd, 'Z');
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 1);
    TEST_ASSERT_EQ(buf[0], 'Z');

    lc_kernel_close_file(rfd);
}

/* ===== lc_print_newline ===== */

static void test_print_newline(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_print_newline(wfd);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 1);
    TEST_ASSERT_EQ(buf[0], '\n');

    lc_kernel_close_file(rfd);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* lc_print_string */
    TEST_RUN(test_print_string);

    /* lc_print_line */
    TEST_RUN(test_print_line);

    /* lc_print_signed */
    TEST_RUN(test_print_signed_positive);
    TEST_RUN(test_print_signed_negative);
    TEST_RUN(test_print_signed_zero);

    /* lc_print_unsigned */
    TEST_RUN(test_print_unsigned_zero);
    TEST_RUN(test_print_unsigned_large);
    TEST_RUN(test_print_unsigned_small);

    /* lc_print_hex */
    TEST_RUN(test_print_hex_zero);
    TEST_RUN(test_print_hex_value);

    /* lc_print_char */
    TEST_RUN(test_print_char);

    /* lc_print_newline */
    TEST_RUN(test_print_newline);

    return test_main();
}
