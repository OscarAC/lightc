#include <lightc/format.h>
#include <lightc/string.h>

/* --- Internal helpers --- */

static void put(lc_format *fmt, char c) {
    if (fmt->used < fmt->capacity) {
        fmt->buffer[fmt->used] = c;
    } else {
        fmt->overflow = true;
    }
    fmt->used++;
}

static void put_bytes(lc_format *fmt, const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        put(fmt, str[i]);
    }
}

/* Render uint64 to a temp buffer, return pointer and length */
static const char *render_unsigned(uint64_t value, char *buf, size_t buf_size, size_t *out_len) {
    char *p = buf + buf_size;
    if (value == 0) {
        *--p = '0';
    } else {
        while (value > 0) {
            *--p = '0' + (value % 10);
            value /= 10;
        }
    }
    *out_len = (size_t)(buf + buf_size - p);
    return p;
}

static const char hex_chars[] = "0123456789abcdef";

static const char *render_hex(uint64_t value, char *buf, size_t buf_size, size_t *out_len) {
    char *p = buf + buf_size;
    if (value == 0) {
        *--p = '0';
    } else {
        while (value > 0) {
            *--p = hex_chars[value & 0xf];
            value >>= 4;
        }
    }
    *out_len = (size_t)(buf + buf_size - p);
    return p;
}

static const char *render_binary(uint64_t value, char *buf, size_t buf_size, size_t *out_len) {
    char *p = buf + buf_size;
    if (value == 0) {
        *--p = '0';
    } else {
        while (value > 0) {
            *--p = '0' + (value & 1);
            value >>= 1;
        }
    }
    *out_len = (size_t)(buf + buf_size - p);
    return p;
}

/* --- Start / Finish --- */

lc_format lc_format_start(char *buffer, size_t capacity) {
    lc_format fmt = {
        .buffer = buffer,
        .capacity = capacity > 0 ? capacity - 1 : 0, /* reserve 1 for null */
        .used = 0,
        .overflow = false
    };
    return fmt;
}

size_t lc_format_finish(lc_format *fmt) {
    /* Null-terminate at the actual write position (clamped to capacity) */
    size_t pos = fmt->used < fmt->capacity + 1 ? fmt->used : fmt->capacity;
    fmt->buffer[pos] = '\0';
    return fmt->used;
}

bool lc_format_has_overflow(const lc_format *fmt) {
    return fmt->overflow;
}

/* --- Add content --- */

void lc_format_add_char(lc_format *fmt, char c) {
    put(fmt, c);
}

void lc_format_add_string(lc_format *fmt, const char *str, size_t length) {
    put_bytes(fmt, str, length);
}

void lc_format_add_text(lc_format *fmt, const char *str) {
    while (*str) {
        put(fmt, *str++);
    }
}

void lc_format_add_newline(lc_format *fmt) {
    put(fmt, '\n');
}

void lc_format_add_repeat(lc_format *fmt, char c, size_t count) {
    for (size_t i = 0; i < count; i++) {
        put(fmt, c);
    }
}

/* --- Numbers --- */

void lc_format_add_unsigned(lc_format *fmt, uint64_t value) {
    char buf[20];
    size_t len;
    const char *p = render_unsigned(value, buf, sizeof(buf), &len);
    put_bytes(fmt, p, len);
}

void lc_format_add_signed(lc_format *fmt, int64_t value) {
    if (value < 0) {
        put(fmt, '-');
        /* Handle INT64_MIN safely */
        lc_format_add_unsigned(fmt, (uint64_t)(-(value + 1)) + 1);
    } else {
        lc_format_add_unsigned(fmt, (uint64_t)value);
    }
}

void lc_format_add_hex(lc_format *fmt, uint64_t value) {
    put(fmt, '0');
    put(fmt, 'x');
    char buf[16];
    size_t len;
    const char *p = render_hex(value, buf, sizeof(buf), &len);
    put_bytes(fmt, p, len);
}

void lc_format_add_hex_raw(lc_format *fmt, uint64_t value) {
    char buf[16];
    size_t len;
    const char *p = render_hex(value, buf, sizeof(buf), &len);
    put_bytes(fmt, p, len);
}

void lc_format_add_binary(lc_format *fmt, uint64_t value) {
    put(fmt, '0');
    put(fmt, 'b');
    char buf[64];
    size_t len;
    const char *p = render_binary(value, buf, sizeof(buf), &len);
    put_bytes(fmt, p, len);
}

void lc_format_add_byte_hex(lc_format *fmt, uint8_t value) {
    put(fmt, hex_chars[value >> 4]);
    put(fmt, hex_chars[value & 0xf]);
}

void lc_format_add_pointer(lc_format *fmt, const void *ptr) {
    lc_format_add_hex(fmt, (uint64_t)(uintptr_t)ptr);
}

void lc_format_add_bool(lc_format *fmt, bool value) {
    if (value) {
        put_bytes(fmt, "true", 4);
    } else {
        put_bytes(fmt, "false", 5);
    }
}

/* --- Padded numbers --- */

void lc_format_add_unsigned_padded(lc_format *fmt, uint64_t value,
                                   uint32_t width, char pad) {
    char buf[20];
    size_t len;
    const char *p = render_unsigned(value, buf, sizeof(buf), &len);

    if (len < width) {
        lc_format_add_repeat(fmt, pad, width - len);
    }
    put_bytes(fmt, p, len);
}

void lc_format_add_signed_padded(lc_format *fmt, int64_t value,
                                 uint32_t width, char pad) {
    /* Render to temp buffer first to know total length */
    char tmp[21]; /* sign + 20 digits */
    size_t tmp_len = 0;

    if (value < 0) {
        tmp[0] = '-';
        tmp_len = 1;
        char nbuf[20];
        size_t nlen;
        uint64_t uval = (uint64_t)(-(value + 1)) + 1;
        const char *p = render_unsigned(uval, nbuf, sizeof(nbuf), &nlen);
        for (size_t i = 0; i < nlen; i++) tmp[tmp_len++] = p[i];
    } else {
        char nbuf[20];
        size_t nlen;
        const char *p = render_unsigned((uint64_t)value, nbuf, sizeof(nbuf), &nlen);
        for (size_t i = 0; i < nlen; i++) tmp[tmp_len++] = p[i];
    }

    if (tmp_len < width) {
        lc_format_add_repeat(fmt, pad, width - tmp_len);
    }
    put_bytes(fmt, tmp, tmp_len);
}

void lc_format_add_hex_padded(lc_format *fmt, uint64_t value, uint32_t width) {
    char buf[16];
    size_t len;
    const char *p = render_hex(value, buf, sizeof(buf), &len);

    if (len < width) {
        lc_format_add_repeat(fmt, '0', width - len);
    }
    put_bytes(fmt, p, len);
}

/* --- Padded strings --- */

void lc_format_add_string_left(lc_format *fmt, const char *str, size_t length,
                               uint32_t width, char pad) {
    put_bytes(fmt, str, length);
    if (length < width) {
        lc_format_add_repeat(fmt, pad, width - length);
    }
}

void lc_format_add_string_right(lc_format *fmt, const char *str, size_t length,
                                uint32_t width, char pad) {
    if (length < width) {
        lc_format_add_repeat(fmt, pad, width - length);
    }
    put_bytes(fmt, str, length);
}
