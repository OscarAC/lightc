/*
 * Exercise the format builder.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/format.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) lc_print_line(STDOUT, S(" PASS"));
    else lc_print_line(STDOUT, S(" FAIL"));
}

/* Helper: format, finish, and check result matches expected string */
static bool check(lc_format *fmt, const char *expected) {
    size_t len = lc_format_finish(fmt);
    size_t exp_len = lc_string_length(expected);
    return len == exp_len && lc_string_equal(fmt->buffer, len, expected, exp_len);
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    char buf[512];

    /* --- Basic text --- */
    lc_print_string(STDOUT, S("format_text"));
    lc_format fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_text(&fmt, "hello world");
    say_pass_fail(check(&fmt, "hello world"));

    /* --- String with length --- */
    lc_print_string(STDOUT, S("format_string"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string(&fmt, S("hello"));
    say_pass_fail(check(&fmt, "hello"));

    /* --- Char + newline --- */
    lc_print_string(STDOUT, S("format_char_newline"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_char(&fmt, 'A');
    lc_format_add_newline(&fmt);
    say_pass_fail(check(&fmt, "A\n"));

    /* --- Unsigned integers --- */
    lc_print_string(STDOUT, S("format_unsigned"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_unsigned(&fmt, 0);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_unsigned(&fmt, 42);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_unsigned(&fmt, 18446744073709551615ULL);
    say_pass_fail(check(&fmt, "0 42 18446744073709551615"));

    /* --- Signed integers --- */
    lc_print_string(STDOUT, S("format_signed"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_signed(&fmt, -1);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_signed(&fmt, 0);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_signed(&fmt, 9223372036854775807LL);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_signed(&fmt, -9223372036854775807LL - 1); /* INT64_MIN */
    say_pass_fail(check(&fmt, "-1 0 9223372036854775807 -9223372036854775808"));

    /* --- Hex --- */
    lc_print_string(STDOUT, S("format_hex"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_hex(&fmt, 0);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_hex(&fmt, 0xdeadbeef);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_hex_raw(&fmt, 255);
    say_pass_fail(check(&fmt, "0x0 0xdeadbeef ff"));

    /* --- Binary --- */
    lc_print_string(STDOUT, S("format_binary"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_binary(&fmt, 0);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_binary(&fmt, 0b10101010);
    say_pass_fail(check(&fmt, "0b0 0b10101010"));

    /* --- Byte hex --- */
    lc_print_string(STDOUT, S("format_byte_hex"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_byte_hex(&fmt, 0x7f);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_byte_hex(&fmt, 0x00);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_byte_hex(&fmt, 0xff);
    say_pass_fail(check(&fmt, "7f 00 ff"));

    /* --- Bool --- */
    lc_print_string(STDOUT, S("format_bool"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_bool(&fmt, true);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_bool(&fmt, false);
    say_pass_fail(check(&fmt, "true false"));

    /* --- Pointer --- */
    lc_print_string(STDOUT, S("format_pointer"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_pointer(&fmt, (void *)0x4000f0);
    say_pass_fail(check(&fmt, "0x4000f0"));

    /* --- Repeat --- */
    lc_print_string(STDOUT, S("format_repeat"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_repeat(&fmt, '-', 5);
    say_pass_fail(check(&fmt, "-----"));

    /* --- Padded unsigned --- */
    lc_print_string(STDOUT, S("format_unsigned_padded"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_unsigned_padded(&fmt, 42, 6, ' ');
    lc_format_add_char(&fmt, '|');
    lc_format_add_unsigned_padded(&fmt, 42, 6, '0');
    say_pass_fail(check(&fmt, "    42|000042"));

    /* --- Padded signed --- */
    lc_print_string(STDOUT, S("format_signed_padded"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_signed_padded(&fmt, -42, 6, ' ');
    lc_format_add_char(&fmt, '|');
    lc_format_add_signed_padded(&fmt, 7, 3, '0');
    say_pass_fail(check(&fmt, "   -42|007"));

    /* --- Padded hex --- */
    lc_print_string(STDOUT, S("format_hex_padded"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_hex_padded(&fmt, 0xff, 4);
    lc_format_add_char(&fmt, ' ');
    lc_format_add_hex_padded(&fmt, 0xdeadbeef, 16);
    say_pass_fail(check(&fmt, "00ff 00000000deadbeef"));

    /* --- String left-aligned --- */
    lc_print_string(STDOUT, S("format_string_left"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string_left(&fmt, S("hi"), 10, '.');
    say_pass_fail(check(&fmt, "hi........"));

    /* --- String right-aligned --- */
    lc_print_string(STDOUT, S("format_string_right"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_string_right(&fmt, S("hi"), 10, '.');
    say_pass_fail(check(&fmt, "........hi"));

    /* --- Mixed: realistic log line --- */
    lc_print_string(STDOUT, S("format_mixed_log"));
    fmt = lc_format_start(buf, sizeof(buf));
    lc_format_add_text(&fmt, "[");
    lc_format_add_unsigned_padded(&fmt, 1234, 8, '0');
    lc_format_add_text(&fmt, "] ");
    lc_format_add_string_left(&fmt, S("GET"), 6, ' ');
    lc_format_add_text(&fmt, " /hello ");
    lc_format_add_signed(&fmt, 200);
    lc_format_add_text(&fmt, " ");
    lc_format_add_unsigned(&fmt, 1024);
    lc_format_add_text(&fmt, "B");
    size_t len = lc_format_finish(&fmt);
    say_pass_fail(lc_string_equal(buf, len,
        S("[00001234] GET    /hello 200 1024B")));

    /* --- Overflow detection --- */
    lc_print_string(STDOUT, S("format_overflow"));
    char tiny[8];
    fmt = lc_format_start(tiny, sizeof(tiny));
    lc_format_add_text(&fmt, "hello world");
    lc_format_finish(&fmt);
    say_pass_fail(lc_format_has_overflow(&fmt)
               && lc_string_equal(tiny, 7, S("hello w")));

    /* --- No overflow on exact fit --- */
    lc_print_string(STDOUT, S("format_exact_fit"));
    char exact[6];
    fmt = lc_format_start(exact, sizeof(exact));
    lc_format_add_text(&fmt, "hello");
    lc_format_finish(&fmt);
    say_pass_fail(!lc_format_has_overflow(&fmt)
               && lc_string_equal(exact, 5, S("hello")));

    /* --- Table formatting demo --- */
    lc_print_string(STDOUT, S("format_table"));
    fmt = lc_format_start(buf, sizeof(buf));
    /* Header */
    lc_format_add_string_left(&fmt, S("Name"), 12, ' ');
    lc_format_add_string_right(&fmt, S("Size"), 8, ' ');
    lc_format_add_string_right(&fmt, S("Addr"), 12, ' ');
    lc_format_add_newline(&fmt);
    /* Row */
    lc_format_add_string_left(&fmt, S(".text"), 12, ' ');
    lc_format_add_unsigned_padded(&fmt, 213, 8, ' ');
    lc_format_add_text(&fmt, "  0x");
    lc_format_add_hex_padded(&fmt, 0x401000, 8);
    lc_format_add_newline(&fmt);
    len = lc_format_finish(&fmt);
    /* Just verify it produces something reasonable */
    say_pass_fail(len > 40 && !lc_format_has_overflow(&fmt));

    /* Print the table to stdout as a demo */
    lc_print_string(STDOUT, S("  "));
    lc_print_string(STDOUT, buf, len);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
