#include <lightc/print.h>
#include <lightc/syscall.h>

void lc_print_char(int32_t fd, char c) {
    lc_kernel_write_bytes(fd, &c, 1);
}

void lc_print_string(int32_t fd, const char *str, size_t length) {
    lc_kernel_write_bytes(fd, str, length);
}

void lc_print_line(int32_t fd, const char *str, size_t length) {
    lc_kernel_write_bytes(fd, str, length);
    lc_print_char(fd, '\n');
}

void lc_print_newline(int32_t fd) {
    lc_print_char(fd, '\n');
}

void lc_print_unsigned(int32_t fd, uint64_t value) {
    /* max uint64_t is 20 digits */
    char buf[20];
    char *p = buf + sizeof(buf);

    if (value == 0) {
        lc_print_char(fd, '0');
        return;
    }

    while (value > 0) {
        *--p = '0' + (value % 10);
        value /= 10;
    }

    lc_kernel_write_bytes(fd, p, (size_t)(buf + sizeof(buf) - p));
}

void lc_print_signed(int32_t fd, int64_t value) {
    if (value < 0) {
        lc_print_char(fd, '-');
        /* Handle INT64_MIN: -(INT64_MIN) overflows, cast first */
        lc_print_unsigned(fd, (uint64_t)(-(value + 1)) + 1);
    } else {
        lc_print_unsigned(fd, (uint64_t)value);
    }
}

static const char hex_digits[] = "0123456789abcdef";

void lc_print_hex(int32_t fd, uint64_t value) {
    char buf[18]; /* "0x" + up to 16 hex digits */
    char *p = buf + sizeof(buf);

    if (value == 0) {
        lc_kernel_write_bytes(fd, "0x0", 3);
        return;
    }

    while (value > 0) {
        *--p = hex_digits[value & 0xf];
        value >>= 4;
    }

    *--p = 'x';
    *--p = '0';

    lc_kernel_write_bytes(fd, p, (size_t)(buf + sizeof(buf) - p));
}

void lc_print_byte_hex(int32_t fd, uint8_t value) {
    char pair[2] = {
        hex_digits[value >> 4],
        hex_digits[value & 0xf]
    };
    lc_kernel_write_bytes(fd, pair, 2);
}
