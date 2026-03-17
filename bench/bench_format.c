/*
 * bench_format.c — format builder and I/O benchmarks.
 *
 * Measures: format builder throughput, buffered writer, pipe I/O.
 */

#include "bench.h"
#include <lightc/format.h>
#include <lightc/io.h>
#include <lightc/syscall.h>

/* --- Format builder --- */

static void bench_format_string(bench_state *b) {
    char buf[256];
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_format fmt = lc_format_start(buf, sizeof(buf));
        lc_format_add_string(&fmt, "hello", 5);
        lc_format_add_char(&fmt, ' ');
        lc_format_add_string(&fmt, "world", 5);
        lc_format_finish(&fmt);
    }
}

static void bench_format_numbers(bench_state *b) {
    char buf[256];
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_format fmt = lc_format_start(buf, sizeof(buf));
        lc_format_add_string(&fmt, "val=", 4);
        lc_format_add_signed(&fmt, 123456789);
        lc_format_add_string(&fmt, " hex=0x", 7);
        lc_format_add_hex(&fmt, 0xDEADBEEF);
        lc_format_finish(&fmt);
    }
}

static void bench_format_complex(bench_state *b) {
    char buf[512];
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_format fmt = lc_format_start(buf, sizeof(buf));
        lc_format_add_string(&fmt, "[", 1);
        lc_format_add_unsigned(&fmt, 42);
        lc_format_add_string(&fmt, "] ", 2);
        lc_format_add_string(&fmt, "request from ", 13);
        lc_format_add_unsigned(&fmt, 192);
        lc_format_add_char(&fmt, '.');
        lc_format_add_unsigned(&fmt, 168);
        lc_format_add_char(&fmt, '.');
        lc_format_add_unsigned(&fmt, 1);
        lc_format_add_char(&fmt, '.');
        lc_format_add_unsigned(&fmt, 100);
        lc_format_add_string(&fmt, " status=", 8);
        lc_format_add_unsigned(&fmt, 200);
        lc_format_add_newline(&fmt);
        lc_format_finish(&fmt);
    }
}

/* --- Buffered writer to /dev/null --- */

static void bench_writer_string(bench_state *b) {
    int32_t fd = (int32_t)lc_kernel_open_file("/dev/null", O_WRONLY, 0);
    if (fd < 0) return;
    lc_writer w = lc_writer_create(fd, 4096);
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_writer_put_string(&w, "hello world\n", 12);
    }
    lc_writer_flush(&w);
    lc_writer_destroy(&w);
    lc_kernel_close_file(fd);
}

/* --- Pipe write+read round-trip --- */

static void bench_pipe_roundtrip(bench_state *b) {
    int32_t fds[2];
    lc_kernel_create_pipe(fds, 0);
    char wbuf[64];
    char rbuf[64];
    lc_bytes_fill(wbuf, 'A', 64);
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_kernel_write_bytes(fds[1], wbuf, 64);
        lc_kernel_read_bytes(fds[0], rbuf, 64);
    }
    lc_kernel_close_file(fds[0]);
    lc_kernel_close_file(fds[1]);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    BENCH_SUITE("format builder");
    BENCH_RUN("strings only",  bench_format_string);
    BENCH_RUN("numbers + hex", bench_format_numbers);
    BENCH_RUN("complex log line", bench_format_complex);

    BENCH_SUITE("buffered I/O");
    BENCH_RUN("writer to /dev/null", bench_writer_string);

    BENCH_SUITE("pipe");
    BENCH_RUN("64B write+read", bench_pipe_roundtrip);

    return 0;
}
