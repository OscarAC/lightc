/*
 * test_log.c — tests for lightc logging module.
 */

#include "test.h"
#include <lightc/log.h>
#include <lightc/syscall.h>
#include <lightc/string.h>

/* ===== Helper: create pipe and redirect log output ===== */

static void setup_log_pipe(int32_t fds[2]) {
    lc_sysret r = lc_kernel_create_pipe(fds, O_NONBLOCK);
    /* fds[0] = read end, fds[1] = write end */
    (void)r;
    lc_log_set_output(fds[1]);
}

static void teardown_log_pipe(int32_t fds[2]) {
    lc_log_set_output(2); /* restore stderr */
    lc_kernel_close_file(fds[0]);
    lc_kernel_close_file(fds[1]);
}

/*
 * Read whatever is available from the pipe into buf.
 * Returns the number of bytes read.
 */
static size_t drain_pipe(int32_t read_fd, char *buf, size_t buf_size) {
    size_t total = 0;
    while (total < buf_size) {
        lc_sysret n = lc_kernel_read_bytes(read_fd, buf + total, buf_size - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    return total;
}

/* ===== lc_log_is_enabled / lc_log_set_level ===== */

static void test_log_set_level(void) {
    /* Set level to WARN */
    lc_log_set_level(LC_LOG_WARN);

    TEST_ASSERT(!lc_log_is_enabled(LC_LOG_DEBUG));
    TEST_ASSERT(!lc_log_is_enabled(LC_LOG_INFO));
    TEST_ASSERT(lc_log_is_enabled(LC_LOG_WARN));
    TEST_ASSERT(lc_log_is_enabled(LC_LOG_ERROR));

    /* Set level to DEBUG — everything enabled */
    lc_log_set_level(LC_LOG_DEBUG);

    TEST_ASSERT(lc_log_is_enabled(LC_LOG_DEBUG));
    TEST_ASSERT(lc_log_is_enabled(LC_LOG_INFO));
    TEST_ASSERT(lc_log_is_enabled(LC_LOG_WARN));
    TEST_ASSERT(lc_log_is_enabled(LC_LOG_ERROR));

    /* Set level to ERROR — only ERROR enabled */
    lc_log_set_level(LC_LOG_ERROR);

    TEST_ASSERT(!lc_log_is_enabled(LC_LOG_DEBUG));
    TEST_ASSERT(!lc_log_is_enabled(LC_LOG_INFO));
    TEST_ASSERT(!lc_log_is_enabled(LC_LOG_WARN));
    TEST_ASSERT(lc_log_is_enabled(LC_LOG_ERROR));

    /* Restore default */
    lc_log_set_level(LC_LOG_DEBUG);
}

/* ===== Level filtering: below-level messages produce no output ===== */

static void test_log_level_filtering(void) {
    int32_t fds[2];
    setup_log_pipe(fds);

    /* Set level to WARN */
    lc_log_set_level(LC_LOG_WARN);
    lc_log_set_format(LC_LOG_FORMAT_TEXT);

    /* DEBUG and INFO should be suppressed */
    lc_log(LC_LOG_DEBUG, "debug message");
    lc_log(LC_LOG_INFO, "info message");

    char buf[512];
    size_t n = drain_pipe(fds[0], buf, sizeof(buf));
    TEST_ASSERT_EQ(n, (size_t)0);

    /* WARN and ERROR should produce output */
    lc_log(LC_LOG_WARN, "warn message");
    lc_log(LC_LOG_ERROR, "error message");

    n = drain_pipe(fds[0], buf, sizeof(buf));
    TEST_ASSERT(n > 0);

    /* Verify WARN output present */
    TEST_ASSERT(lc_string_contains(buf, n, "warn message", 12));

    /* Verify ERROR output present */
    TEST_ASSERT(lc_string_contains(buf, n, "error message", 13));

    /* Restore default */
    lc_log_set_level(LC_LOG_DEBUG);
    teardown_log_pipe(fds);
}

/* ===== Text format contains expected markers ===== */

static void test_log_text_format(void) {
    int32_t fds[2];
    setup_log_pipe(fds);

    lc_log_set_level(LC_LOG_DEBUG);
    lc_log_set_format(LC_LOG_FORMAT_TEXT);

    lc_log(LC_LOG_INFO, "server started");

    char buf[512];
    size_t n = drain_pipe(fds[0], buf, sizeof(buf));
    TEST_ASSERT(n > 0);

    /* Should contain the level tag */
    TEST_ASSERT(lc_string_contains(buf, n, "[INFO", 5));

    /* Should contain the message text */
    TEST_ASSERT(lc_string_contains(buf, n, "server started", 14));

    /* Should contain tid: marker */
    TEST_ASSERT(lc_string_contains(buf, n, "tid:", 4));

    teardown_log_pipe(fds);
}

/* ===== JSON format contains expected fields ===== */

static void test_log_json_format(void) {
    int32_t fds[2];
    setup_log_pipe(fds);

    lc_log_set_level(LC_LOG_DEBUG);
    lc_log_set_format(LC_LOG_FORMAT_JSON);

    lc_log(LC_LOG_INFO, "json test msg");

    char buf[1024];
    size_t n = drain_pipe(fds[0], buf, sizeof(buf));
    TEST_ASSERT(n > 0);

    /* Should contain level field */
    TEST_ASSERT(lc_string_contains(buf, n, "\"level\":\"INFO\"", 14));

    /* Should contain the message field */
    TEST_ASSERT(lc_string_contains(buf, n, "\"msg\":\"json test msg\"", 21));

    /* Should contain ts field */
    TEST_ASSERT(lc_string_contains(buf, n, "\"ts\":", 5));

    /* Should contain tid field */
    TEST_ASSERT(lc_string_contains(buf, n, "\"tid\":", 6));

    /* Restore text format */
    lc_log_set_format(LC_LOG_FORMAT_TEXT);
    teardown_log_pipe(fds);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* lc_log_set_level / lc_log_is_enabled */
    TEST_RUN(test_log_set_level);

    /* level filtering */
    TEST_RUN(test_log_level_filtering);

    /* text format output */
    TEST_RUN(test_log_text_format);

    /* JSON format output */
    TEST_RUN(test_log_json_format);

    return test_main();
}
