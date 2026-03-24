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

/* --- Floating-point --- */

static const uint64_t pow10_table[] = {
    1ULL,                    /* 0 */
    10ULL,                   /* 1 */
    100ULL,                  /* 2 */
    1000ULL,                 /* 3 */
    10000ULL,                /* 4 */
    100000ULL,               /* 5 */
    1000000ULL,              /* 6 */
    10000000ULL,             /* 7 */
    100000000ULL,            /* 8 */
    1000000000ULL,           /* 9 */
    10000000000ULL,          /* 10 */
    100000000000ULL,         /* 11 */
    1000000000000ULL,        /* 12 */
    10000000000000ULL,       /* 13 */
    100000000000000ULL,      /* 14 */
    1000000000000000ULL,     /* 15 */
    10000000000000000ULL,    /* 16 */
    100000000000000000ULL,   /* 17 */
    1000000000000000000ULL,  /* 18 */
};

void lc_format_add_double_precision(lc_format *fmt, double value, uint32_t precision) {
    /* Type-pun to inspect bits */
    uint64_t bits;
    __builtin_memcpy(&bits, &value, 8);

    uint32_t sign = (uint32_t)(bits >> 63);
    uint32_t exponent = (uint32_t)((bits >> 52) & 0x7FF);
    uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;

    /* Handle special cases */
    if (exponent == 0x7FF) {
        if (mantissa != 0) {
            put_bytes(fmt, "nan", 3);
        } else if (sign) {
            put_bytes(fmt, "-inf", 4);
        } else {
            put_bytes(fmt, "inf", 3);
        }
        return;
    }

    /* Handle negative zero */
    if (sign && exponent == 0 && mantissa == 0) {
        put(fmt, '-');
        put(fmt, '0');
        if (precision > 0) {
            put(fmt, '.');
            for (uint32_t i = 0; i < precision; i++) {
                put(fmt, '0');
            }
        }
        return;
    }

    /* Handle sign */
    if (sign) {
        put(fmt, '-');
        value = -value;
    }

    /* Clamp precision to table size */
    if (precision > 18) {
        precision = 18;
    }

    /* Handle very large values that would overflow uint64_t */
    if (value >= 18446744073709551616.0) {
        put_bytes(fmt, "inf", 3);
        return;
    }

    /* Precision 0: round to nearest integer, no decimal point */
    if (precision == 0) {
        uint64_t rounded = (uint64_t)(value + 0.5);
        char buf[20];
        size_t len;
        const char *p = render_unsigned(rounded, buf, sizeof(buf), &len);
        put_bytes(fmt, p, len);
        return;
    }

    uint64_t pow10 = pow10_table[precision];

    uint64_t int_part = (uint64_t)value;
    double frac = value - (double)int_part;
    uint64_t frac_int = (uint64_t)(frac * (double)pow10 + 0.5);

    /* Handle rounding overflow (e.g. 0.9999... rounds up) */
    if (frac_int >= pow10) {
        int_part++;
        frac_int = 0;
    }

    /* Render integer part */
    char ibuf[20];
    size_t ilen;
    const char *ip = render_unsigned(int_part, ibuf, sizeof(ibuf), &ilen);
    put_bytes(fmt, ip, ilen);

    /* Decimal point */
    put(fmt, '.');

    /* Render fractional part, zero-padded to precision digits */
    char fbuf[20];
    size_t flen;
    const char *fp = render_unsigned(frac_int, fbuf, sizeof(fbuf), &flen);

    /* Leading zeros */
    if (flen < precision) {
        for (uint32_t i = 0; i < precision - (uint32_t)flen; i++) {
            put(fmt, '0');
        }
    }
    put_bytes(fmt, fp, flen);
}

void lc_format_add_double(lc_format *fmt, double value) {
    lc_format_add_double_precision(fmt, value, 6);
}
