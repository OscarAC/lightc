#include <lightio/lightio.h>
#include <lightc/print.h>
#include <lightc/string.h>
#include <lightc/format.h>
#include <lightc/syscall.h>
#include <lightc/time.h>
#include <stdatomic.h>

static _Atomic(uint64_t) request_count = 0;

static void handle_http(lio_stream *stream) {
    char request[4096];

    int32_t n = lio_read(stream, request, sizeof(request) - 1);
    if (n <= 0) return;
    request[n] = '\0';

    atomic_fetch_add(&request_count, 1);

    /* Parse method and path (minimal) */
    const char *path = "/";
    size_t path_len = 1;
    if (n > 4 && request[0] == 'G' && request[1] == 'E' &&
        request[2] == 'T' && request[3] == ' ') {
        path = request + 4;
        const char *end = path;
        while (*end && *end != ' ' && *end != '\r') end++;
        path_len = (size_t)(end - path);
    }

    /* Build response body */
    char body[4096];
    lc_format body_fmt = lc_format_start(body, sizeof(body));

    const char *status = "200 OK";
    const char *content_type = "text/plain";

    if (path_len == 1 && path[0] == '/') {
        content_type = "text/html";
        lc_format_add_text(&body_fmt,
            "<html><body style='font-family:monospace;"
            "background:#0a0a0a;color:#e0e0e0;padding:2em'>");
        lc_format_add_text(&body_fmt, "<h1>lightio</h1>");
        lc_format_add_text(&body_fmt,
            "<p>Async I/O framework — coroutines + io_uring, zero libc</p>");
        lc_format_add_text(&body_fmt, "<p>Requests served: ");
        lc_format_add_unsigned(&body_fmt, atomic_load(&request_count));
        lc_format_add_text(&body_fmt, "</p>");

        /* Current time */
        char timebuf[32];
        lc_date_time now = lc_time_now();
        lc_time_format_iso8601(&now, timebuf, sizeof(timebuf));
        lc_format_add_text(&body_fmt, "<p>Time: ");
        lc_format_add_text(&body_fmt, timebuf);
        lc_format_add_text(&body_fmt, "</p>");

        lc_format_add_text(&body_fmt,
            "<p><a href='/stats' style='color:#6cf'>/stats</a></p>");
        lc_format_add_text(&body_fmt, "</body></html>");
    } else if (path_len == 6 &&
               lc_string_starts_with(path, path_len, "/stats", 6)) {
        content_type = "application/json";
        lc_format_add_text(&body_fmt, "{\"requests\":");
        lc_format_add_unsigned(&body_fmt, atomic_load(&request_count));
        lc_format_add_text(&body_fmt, "}");
    } else {
        status = "404 Not Found";
        lc_format_add_text(&body_fmt, "not found");
    }

    size_t body_len = lc_format_finish(&body_fmt);

    /* Build full HTTP response */
    char response[8192];
    lc_format fmt = lc_format_start(response, sizeof(response));

    lc_format_add_text(&fmt, "HTTP/1.1 ");
    lc_format_add_text(&fmt, status);
    lc_format_add_text(&fmt, "\r\nContent-Type: ");
    lc_format_add_text(&fmt, content_type);
    lc_format_add_text(&fmt, "\r\nContent-Length: ");
    lc_format_add_unsigned(&fmt, body_len);
    lc_format_add_text(&fmt, "\r\nConnection: close\r\n\r\n");
    lc_format_add_string(&fmt, body, body_len);

    size_t resp_len = lc_format_finish(&fmt);

    lio_write(stream, response, (uint32_t)resp_len);
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    uint16_t port = 8080;

    if (argc > 1) {
        /* Simple port parsing */
        port = 0;
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; p++)
            port = port * 10 + (*p - '0');
    }

    lio_loop *loop = lio_loop_create();
    if (!loop) {
        lc_print_line(STDERR, "failed to create event loop", 27);
        return 1;
    }

    if (lc_is_err(lio_tcp_serve(loop, port, handle_http))) {
        lc_print_line(STDERR, "failed to start server", 22);
        lio_loop_destroy(loop);
        return 1;
    }

    char msg[64];
    lc_format fmt = lc_format_start(msg, sizeof(msg));
    lc_format_add_text(&fmt, "lightio http server on port ");
    lc_format_add_unsigned(&fmt, port);
    lc_print_line(STDOUT, msg, lc_format_finish(&fmt));

    lio_loop_run(loop);

    lio_loop_destroy(loop);
    return 0;
}
