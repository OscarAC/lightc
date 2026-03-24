#include <lightio/lightio.h>
#include <lightc/print.h>
#include <lightc/string.h>
#include <lightc/format.h>
#include <lightc/syscall.h>

#define S(literal) literal, sizeof(literal) - 1

static void handle_echo(lio_stream *stream) {
    char buf[4096];
    char msg[128];

    /* Log connection */
    lc_format fmt = lc_format_start(msg, sizeof(msg));
    lc_format_add_text(&fmt, "[echo] client connected (fd ");
    lc_format_add_signed(&fmt, lio_stream_fd(stream));
    lc_format_add_text(&fmt, ")");
    size_t len = lc_format_finish(&fmt);
    lc_print_line(STDOUT, msg, len);

    /* Echo loop: read data, echo it back */
    while (true) {
        int32_t n = lio_read(stream, buf, sizeof(buf));
        if (n <= 0) break;  /* client disconnected or error */

        int32_t w = lio_write(stream, buf, (uint32_t)n);
        if (w <= 0) break;  /* write error */
    }

    fmt = lc_format_start(msg, sizeof(msg));
    lc_format_add_text(&fmt, "[echo] client disconnected (fd ");
    lc_format_add_signed(&fmt, lio_stream_fd(stream));
    lc_format_add_text(&fmt, ")");
    len = lc_format_finish(&fmt);
    lc_print_line(STDOUT, msg, len);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    uint16_t port = 9000;

    lio_loop *loop = lio_loop_create();
    if (!loop) {
        lc_print_line(STDERR, S("failed to create event loop"));
        return 1;
    }

    if (lc_is_err(lio_tcp_serve(loop, port, handle_echo))) {
        lc_print_line(STDERR, S("failed to start server"));
        lio_loop_destroy(loop);
        return 1;
    }

    char msg[64];
    lc_format fmt = lc_format_start(msg, sizeof(msg));
    lc_format_add_text(&fmt, "lightio echo server on port ");
    lc_format_add_unsigned(&fmt, port);
    lc_print_line(STDOUT, msg, lc_format_finish(&fmt));

    lio_loop_run(loop);

    lio_loop_destroy(loop);
    return 0;
}
