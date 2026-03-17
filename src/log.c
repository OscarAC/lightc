#include <lightc/log.h>
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/thread.h>
#include <lightc/format.h>

/* Global state */
static lc_log_level  log_min_level = LC_LOG_DEBUG;
static int32_t       log_fd = 2;  /* STDERR */
static lc_spinlock   log_lock = LC_SPINLOCK_INIT;
static lc_log_format log_format = LC_LOG_FORMAT_TEXT;

static const char *level_tags[] = {
    "[DEBUG ",
    "[INFO  ",
    "[WARN  ",
    "[ERROR "
};

static const size_t level_tag_lens[] = { 7, 7, 7, 7 };

/* --- Configuration --- */

void lc_log_set_level(lc_log_level min_level) {
    log_min_level = min_level;
}

void lc_log_set_output(int32_t fd) {
    log_fd = fd;
}

void lc_log_set_format(lc_log_format format) {
    log_format = format;
}

bool lc_log_is_enabled(lc_log_level level) {
    return level >= log_min_level;
}

static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

static const size_t level_name_lens[] = { 5, 4, 4, 5 };

/*
 * Build the log prefix into a buffer:
 *   [LEVEL tid:NNNN]
 * Returns the length written.
 */
static size_t build_prefix(char *buf, size_t buf_size, lc_log_level level) {
    lc_format fmt = lc_format_start(buf, buf_size);
    lc_format_add_string(&fmt, level_tags[level], level_tag_lens[level]);
    lc_format_add_text(&fmt, "tid:");
    lc_format_add_unsigned(&fmt, (uint64_t)(uint32_t)lc_kernel_get_thread_id());
    lc_format_add_text(&fmt, "] ");
    return lc_format_finish(&fmt);
}

/*
 * Add a JSON-escaped version of the message to the format builder.
 * Escapes: " -> \", \ -> \\, \n -> \\n, \r -> \\r, \t -> \\t
 */
static void format_add_json_escaped(lc_format *fmt, const char *msg, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = msg[i];
        switch (c) {
        case '"':
            lc_format_add_string(fmt, "\\\"", 2);
            break;
        case '\\':
            lc_format_add_string(fmt, "\\\\", 2);
            break;
        case '\n':
            lc_format_add_string(fmt, "\\n", 2);
            break;
        case '\r':
            lc_format_add_string(fmt, "\\r", 2);
            break;
        case '\t':
            lc_format_add_string(fmt, "\\t", 2);
            break;
        default:
            lc_format_add_char(fmt, c);
            break;
        }
    }
}

/*
 * Write a JSON-formatted log line atomically.
 * {"ts":<seconds>,"level":"...","msg":"...","tid":<tid>}
 */
static void write_json_line(lc_log_level level, const char *message, size_t msg_len) {
    char buf[1024];
    lc_format fmt = lc_format_start(buf, sizeof(buf));

    /* Get timestamp */
    lc_timespec ts;
    lc_kernel_get_clock(LC_CLOCK_REALTIME, &ts);

    /* Build JSON */
    lc_format_add_text(&fmt, "{\"ts\":");
    lc_format_add_signed(&fmt, ts.seconds);
    lc_format_add_text(&fmt, ",\"level\":\"");
    lc_format_add_string(&fmt, level_names[level], level_name_lens[level]);
    lc_format_add_text(&fmt, "\",\"msg\":\"");
    format_add_json_escaped(&fmt, message, msg_len);
    lc_format_add_text(&fmt, "\",\"tid\":");
    lc_format_add_unsigned(&fmt, (uint64_t)(uint32_t)lc_kernel_get_thread_id());
    lc_format_add_text(&fmt, "}\n");

    size_t len = lc_format_finish(&fmt);
    if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;

    /* Write atomically under the spinlock */
    lc_spinlock_acquire(&log_lock);
    lc_kernel_write_bytes(log_fd, buf, len);
    lc_spinlock_release(&log_lock);
}

/*
 * Write a complete log line atomically.
 * Lock ensures no interleaving between threads.
 */
static void write_log_line(lc_log_level level, const char *message, size_t msg_len) {
    if (log_format == LC_LOG_FORMAT_JSON) {
        write_json_line(level, message, msg_len);
        return;
    }

    /* Build prefix on the stack */
    char prefix[48];
    size_t prefix_len = build_prefix(prefix, sizeof(prefix), level);

    /* Write atomically: prefix + message + newline */
    lc_spinlock_acquire(&log_lock);
    lc_kernel_write_bytes(log_fd, prefix, prefix_len);
    lc_kernel_write_bytes(log_fd, message, msg_len);
    lc_kernel_write_bytes(log_fd, "\n", 1);
    lc_spinlock_release(&log_lock);
}

/* --- Simple logging --- */

void lc_log(lc_log_level level, const char *message) {
    if (level < log_min_level) return;
    lc_log_string(level, message, lc_string_length(message));
}

void lc_log_string(lc_log_level level, const char *message, size_t length) {
    if (level < log_min_level) return;
    write_log_line(level, message, length);
}

/* --- Formatted logging --- */

void lc_log_begin(lc_log_entry *entry, lc_log_level level) {
    entry->level = level;
    if (level < log_min_level) {
        entry->fmt = lc_format_start(entry->buffer, 0);
    } else {
        entry->fmt = lc_format_start(entry->buffer, LC_LOG_BUFFER_SIZE);
    }
}

void lc_log_commit(lc_log_entry *entry) {
    if (entry->level < log_min_level) return;

    size_t msg_len = lc_format_finish(&entry->fmt);
    /* Clamp to buffer if overflow */
    if (msg_len > LC_LOG_BUFFER_SIZE - 1) msg_len = LC_LOG_BUFFER_SIZE - 1;

    write_log_line(entry->level, entry->buffer, msg_len);
}
