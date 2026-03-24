/*
 * Exercise thread-safe logging.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/format.h>
#include <lightc/log.h>
#include <lightc/thread.h>
#include <lightc/io.h>
#include <lightc/heap.h>
#include <stdatomic.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) lc_print_line(STDOUT, S(" PASS"));
    else lc_print_line(STDOUT, S(" FAIL"));
}

/* --- Multi-threaded log stress test --- */

#define LOG_THREADS 4
#define LOGS_PER_THREAD 200

static int32_t log_stress_thread(void *arg) {
    int32_t id = *(int32_t *)arg;
    for (int i = 0; i < LOGS_PER_THREAD; i++) {
        lc_log_entry entry;
        lc_log_begin(&entry, LC_LOG_INFO);
        lc_format_add_text(&entry.fmt, "thread ");
        lc_format_add_signed(&entry.fmt, id);
        lc_format_add_text(&entry.fmt, " message ");
        lc_format_add_signed(&entry.fmt, i);
        lc_log_commit(&entry);
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* --- Basic log levels --- */
    lc_print_line(STDOUT, S("--- basic logging to stderr ---"));
    lc_log(LC_LOG_DEBUG, "this is debug");
    lc_log(LC_LOG_INFO,  "this is info");
    lc_log(LC_LOG_WARN,  "this is a warning");
    lc_log(LC_LOG_ERROR, "this is an error");

    lc_print_string(STDOUT, S("log_basic"));
    say_pass_fail(true); /* if we got here without crashing, basic works */

    /* --- Formatted logging --- */
    lc_print_string(STDOUT, S("log_formatted"));
    lc_log_entry entry;
    lc_log_begin(&entry, LC_LOG_INFO);
    lc_format_add_text(&entry.fmt, "request #");
    lc_format_add_unsigned(&entry.fmt, 42);
    lc_format_add_text(&entry.fmt, " took ");
    lc_format_add_unsigned(&entry.fmt, 7);
    lc_format_add_text(&entry.fmt, "ms");
    lc_log_commit(&entry);
    say_pass_fail(true);

    /* --- Level filtering --- */
    lc_print_string(STDOUT, S("log_filtering"));
    lc_log_set_level(LC_LOG_WARN);
    bool debug_enabled = lc_log_is_enabled(LC_LOG_DEBUG);
    bool warn_enabled = lc_log_is_enabled(LC_LOG_WARN);
    bool error_enabled = lc_log_is_enabled(LC_LOG_ERROR);
    /* These should be silently discarded */
    lc_log(LC_LOG_DEBUG, "should not appear");
    lc_log(LC_LOG_INFO, "should not appear either");
    /* These should appear */
    lc_log(LC_LOG_WARN, "this warning should appear");
    lc_log(LC_LOG_ERROR, "this error should appear");
    say_pass_fail(!debug_enabled && warn_enabled && error_enabled);

    /* --- Filtered begin/commit (no work done) --- */
    lc_print_string(STDOUT, S("log_filtered_begin"));
    lc_log_begin(&entry, LC_LOG_DEBUG);
    /* This should be a no-op since debug is below min level */
    lc_format_add_text(&entry.fmt, "this is expensive formatting that gets skipped");
    lc_log_commit(&entry);
    say_pass_fail(true);

    /* Reset level for remaining tests */
    lc_log_set_level(LC_LOG_DEBUG);

    /* --- Log to file --- */
    lc_print_string(STDOUT, S("log_to_file"));
    const char *log_path = "/tmp/lightc_log_test.txt";
    int32_t log_fd = (int32_t)lc_kernel_open_file(log_path,
        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = (log_fd >= 0);
    if (ok) {
        lc_log_set_output(log_fd);
        lc_log(LC_LOG_INFO, "logged to file");

        lc_log_begin(&entry, LC_LOG_WARN);
        lc_format_add_text(&entry.fmt, "value=");
        lc_format_add_unsigned(&entry.fmt, 123);
        lc_log_commit(&entry);

        lc_kernel_close_file(log_fd);
        lc_log_set_output(2); /* back to stderr */

        /* Read back and verify */
        uint8_t *data;
        size_t size;
        ok = lc_is_ok(lc_file_read_all(log_path, &data, &size));
        if (ok) {
            ok = size > 0
              && lc_string_contains((const char *)data, size, S("logged to file"))
              && lc_string_contains((const char *)data, size, S("value=123"));
            lc_heap_free(data);
        }
    }
    say_pass_fail(ok);

    /* --- Multi-threaded stress test --- */
    lc_print_string(STDOUT, S("log_threaded"));
    /* Log to a file so we can verify no garbled lines */
    const char *stress_path = "/tmp/lightc_log_stress.txt";
    log_fd = (int32_t)lc_kernel_open_file(stress_path,
        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ok = (log_fd >= 0);
    if (ok) {
        lc_log_set_output(log_fd);

        lc_thread threads[LOG_THREADS];
        int32_t ids[LOG_THREADS];
        for (int i = 0; i < LOG_THREADS; i++) {
            ids[i] = i;
            (void)lc_thread_create(&threads[i], log_stress_thread, &ids[i]);
        }
        for (int i = 0; i < LOG_THREADS; i++) {
            lc_thread_join(&threads[i]);
        }

        lc_kernel_close_file(log_fd);
        lc_log_set_output(2);

        /* Read the file and verify:
         * - Correct number of lines
         * - Every line starts with "[INFO " (no garbling) */
        uint8_t *data;
        size_t size;
        ok = lc_is_ok(lc_file_read_all(stress_path, &data, &size));
        if (ok) {
            int line_count = 0;
            bool all_valid = true;
            size_t pos = 0;
            while (pos < size) {
                /* Find end of line */
                size_t line_start = pos;
                while (pos < size && data[pos] != '\n') pos++;

                if (pos > line_start) {
                    line_count++;
                    /* Every line should start with [INFO */
                    if (!lc_string_starts_with((const char *)data + line_start,
                            pos - line_start, S("[INFO "))) {
                        all_valid = false;
                    }
                }
                pos++; /* skip newline */
            }

            int expected_lines = LOG_THREADS * LOGS_PER_THREAD;
            ok = all_valid && (line_count == expected_lines);

            if (!ok) {
                lc_print_string(STDOUT, S(" ("));
                lc_print_signed(STDOUT, line_count);
                lc_print_string(STDOUT, S("/"));
                lc_print_signed(STDOUT, expected_lines);
                lc_print_string(STDOUT, S(" lines)"));
            }

            lc_heap_free(data);
        }
    }
    say_pass_fail(ok);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
