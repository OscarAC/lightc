/*
 * test_io.c — tests for lightc buffered I/O (writer + reader).
 */

#include "test.h"
#include <lightc/io.h>
#include <lightc/syscall.h>
#include <lightc/string.h>

/* Helper: create a pipe pair, returning 0 on success. */
static int pipe_create(int32_t *read_fd, int32_t *write_fd) {
    int32_t fds[2];
    lc_sysret ret = lc_kernel_create_pipe(fds, 0);
    if (ret < 0) return -1;
    *read_fd  = fds[0];
    *write_fd = fds[1];
    return 0;
}

/* Helper: read exactly `count` bytes from a raw fd into buf. */
static size_t raw_read(int32_t fd, char *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        lc_sysret ret = lc_kernel_read_bytes(fd, buf + total, count - total);
        if (ret <= 0) break;
        total += (size_t)ret;
    }
    return total;
}

/* ===== Writer lifecycle ===== */

static void test_writer_create_destroy(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);
    TEST_ASSERT_NOT_NULL(w.buffer);
    TEST_ASSERT_EQ(w.fd, wfd);
    TEST_ASSERT_EQ(w.capacity, 256);
    TEST_ASSERT_EQ(w.used, 0);

    lc_writer_destroy(&w);
    TEST_ASSERT_NULL(w.buffer);
    TEST_ASSERT_EQ(w.capacity, 0);
    TEST_ASSERT_EQ(w.used, 0);

    lc_kernel_close_file(rfd);
}

/* ===== Writer: put_string + flush ===== */

static void test_writer_put_string_flush(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);
    lc_writer_put_string(&w, "hello", 5);
    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 5);
    TEST_ASSERT_STR_EQ(buf, 5, "hello", 5);

    lc_kernel_close_file(rfd);
    /* buffer already freed by close; free manually since we flushed ourselves */
    lc_writer_destroy(&w);
}

/* ===== Writer: put_byte ===== */

static void test_writer_put_byte(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);
    lc_writer_put_byte(&w, 0x41); /* 'A' */
    lc_writer_put_byte(&w, 0x42); /* 'B' */
    lc_writer_put_byte(&w, 0x43); /* 'C' */
    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 3);
    TEST_ASSERT_STR_EQ(buf, 3, "ABC", 3);

    lc_kernel_close_file(rfd);
    lc_writer_destroy(&w);
}

/* ===== Writer: put_char ===== */

static void test_writer_put_char(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);
    lc_writer_put_char(&w, 'X');
    lc_writer_put_char(&w, 'Y');
    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 2);
    TEST_ASSERT_STR_EQ(buf, 2, "XY", 2);

    lc_kernel_close_file(rfd);
    lc_writer_destroy(&w);
}

/* ===== Writer: put_line ===== */

static void test_writer_put_line(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);
    lc_writer_put_line(&w, "hello", 5);
    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_EQ(n, 6);
    TEST_ASSERT_STR_EQ(buf, 6, "hello\n", 6);

    lc_kernel_close_file(rfd);
    lc_writer_destroy(&w);
}

/* ===== Writer: put_signed ===== */

static void test_writer_put_signed(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);

    /* positive */
    lc_writer_put_signed(&w, 42);
    lc_writer_put_char(&w, ',');

    /* negative */
    lc_writer_put_signed(&w, -99);
    lc_writer_put_char(&w, ',');

    /* zero */
    lc_writer_put_signed(&w, 0);

    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "42,-99,0", 8);

    lc_kernel_close_file(rfd);
    lc_writer_destroy(&w);
}

/* ===== Writer: put_unsigned ===== */

static void test_writer_put_unsigned(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_writer w = lc_writer_create(wfd, 256);

    lc_writer_put_unsigned(&w, 0);
    lc_writer_put_char(&w, ',');
    lc_writer_put_unsigned(&w, 12345);
    lc_writer_put_char(&w, ',');
    lc_writer_put_unsigned(&w, 18446744073709551615ULL);

    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);

    char buf[64];
    size_t n = raw_read(rfd, buf, sizeof(buf));
    TEST_ASSERT_STR_EQ(buf, n, "0,12345,18446744073709551615", 28);

    lc_kernel_close_file(rfd);
    lc_writer_destroy(&w);
}

/* ===== Reader lifecycle ===== */

static void test_reader_create_destroy(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_reader r = lc_reader_create(rfd, 128);
    TEST_ASSERT_NOT_NULL(r.buffer);
    TEST_ASSERT_EQ(r.fd, rfd);
    TEST_ASSERT_EQ(r.capacity, 128);
    TEST_ASSERT_EQ(r.filled, 0);
    TEST_ASSERT_EQ(r.position, 0);
    TEST_ASSERT(!r.end_of_file);

    lc_reader_destroy(&r);
    TEST_ASSERT_NULL(r.buffer);

    lc_kernel_close_file(rfd);
    lc_kernel_close_file(wfd);
}

/* ===== Reader: read_byte ===== */

static void test_reader_read_byte(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    /* Write raw bytes into pipe */
    lc_kernel_write_bytes(wfd, "ABC", 3);
    lc_kernel_close_file(wfd);

    lc_reader r = lc_reader_create(rfd, 128);
    TEST_ASSERT_EQ(lc_reader_read_byte(&r), 'A');
    TEST_ASSERT_EQ(lc_reader_read_byte(&r), 'B');
    TEST_ASSERT_EQ(lc_reader_read_byte(&r), 'C');
    TEST_ASSERT_EQ(lc_reader_read_byte(&r), -1); /* EOF */

    lc_reader_destroy(&r);
    lc_kernel_close_file(rfd);
}

/* ===== Reader: read_line ===== */

static void test_reader_read_line(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    lc_kernel_write_bytes(wfd, "first\nsecond\n", 13);
    lc_kernel_close_file(wfd);

    lc_reader r = lc_reader_create(rfd, 128);
    char line[64];

    int64_t len1 = lc_reader_read_line(&r, line, sizeof(line));
    TEST_ASSERT_EQ(len1, 5);
    TEST_ASSERT_STR_EQ(line, 5, "first", 5);

    int64_t len2 = lc_reader_read_line(&r, line, sizeof(line));
    TEST_ASSERT_EQ(len2, 6);
    TEST_ASSERT_STR_EQ(line, 6, "second", 6);

    /* EOF — no more data */
    int64_t len3 = lc_reader_read_line(&r, line, sizeof(line));
    TEST_ASSERT_EQ(len3, -1);

    lc_reader_destroy(&r);
    lc_kernel_close_file(rfd);
}

/* ===== Writer+Reader round-trip ===== */

static void test_writer_reader_roundtrip(void) {
    int32_t rfd, wfd;
    TEST_ASSERT_EQ(pipe_create(&rfd, &wfd), 0);

    /* Write via buffered writer */
    lc_writer w = lc_writer_create(wfd, 64);
    lc_writer_put_string(&w, "line1\n", 6);
    lc_writer_put_string(&w, "line2\n", 6);
    lc_writer_put_signed(&w, -42);
    lc_writer_put_char(&w, '\n');
    lc_writer_flush(&w);
    lc_kernel_close_file(wfd);
    /* Destroy after closing fd to avoid double-flush to closed fd */
    w.fd = -1;
    lc_writer_destroy(&w);

    /* Read via buffered reader */
    lc_reader r = lc_reader_create(rfd, 64);
    char line[64];

    int64_t n1 = lc_reader_read_line(&r, line, sizeof(line));
    TEST_ASSERT_EQ(n1, 5);
    TEST_ASSERT_STR_EQ(line, 5, "line1", 5);

    int64_t n2 = lc_reader_read_line(&r, line, sizeof(line));
    TEST_ASSERT_EQ(n2, 5);
    TEST_ASSERT_STR_EQ(line, 5, "line2", 5);

    int64_t n3 = lc_reader_read_line(&r, line, sizeof(line));
    TEST_ASSERT_EQ(n3, 3);
    TEST_ASSERT_STR_EQ(line, 3, "-42", 3);

    lc_reader_destroy(&r);
    lc_kernel_close_file(rfd);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Writer lifecycle */
    TEST_RUN(test_writer_create_destroy);

    /* Writer output */
    TEST_RUN(test_writer_put_string_flush);
    TEST_RUN(test_writer_put_byte);
    TEST_RUN(test_writer_put_char);
    TEST_RUN(test_writer_put_line);
    TEST_RUN(test_writer_put_signed);
    TEST_RUN(test_writer_put_unsigned);

    /* Reader lifecycle */
    TEST_RUN(test_reader_create_destroy);

    /* Reader input */
    TEST_RUN(test_reader_read_byte);
    TEST_RUN(test_reader_read_line);

    /* Round-trip */
    TEST_RUN(test_writer_reader_roundtrip);

    return test_main();
}
