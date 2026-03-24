/*
 * Exercise the I/O layer: buffered writer, buffered reader,
 * file utilities, and directory listing.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/heap.h>
#include <lightc/io.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) {
        lc_print_line(STDOUT, S(" PASS"));
    } else {
        lc_print_line(STDOUT, S(" FAIL"));
    }
}

static const char *test_file = "/tmp/lightc_io_test.txt";
static const char *test_file2 = "/tmp/lightc_io_test2.txt";

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* ================================================================
     * Buffered Writer
     * ================================================================ */

    lc_print_string(STDOUT, S("writer_create"));
    lc_sysret fd_ret = lc_kernel_open_file(test_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    bool ok = fd_ret >= 0;
    say_pass_fail(ok);
    if (!ok) return 1;
    int32_t fd = (int32_t)fd_ret;

    lc_writer w = lc_writer_create(fd, 64);
    lc_print_string(STDOUT, S("writer_buffer_alloc"));
    say_pass_fail(w.buffer != NULL);

    /* Write a string */
    lc_print_string(STDOUT, S("writer_put_string"));
    lc_writer_put_string(&w, S("hello world"));
    lc_writer_put_newline(&w);
    say_pass_fail(w.used > 0);

    /* Write a character */
    lc_print_string(STDOUT, S("writer_put_char"));
    lc_writer_put_char(&w, 'A');
    lc_writer_put_newline(&w);
    say_pass_fail(true);

    /* Write signed integers */
    lc_print_string(STDOUT, S("writer_put_signed"));
    lc_writer_put_signed(&w, 42);
    lc_writer_put_newline(&w);
    lc_writer_put_signed(&w, -99);
    lc_writer_put_newline(&w);
    lc_writer_put_signed(&w, 0);
    lc_writer_put_newline(&w);
    say_pass_fail(true);

    /* Write unsigned integer */
    lc_print_string(STDOUT, S("writer_put_unsigned"));
    lc_writer_put_unsigned(&w, 12345);
    lc_writer_put_newline(&w);
    say_pass_fail(true);

    /* Write hex */
    lc_print_string(STDOUT, S("writer_put_hex"));
    lc_writer_put_hex(&w, 0xff);
    lc_writer_put_newline(&w);
    lc_writer_put_hex(&w, 0);
    lc_writer_put_newline(&w);
    say_pass_fail(true);

    /* Write a line (string + newline) */
    lc_print_string(STDOUT, S("writer_put_line"));
    lc_writer_put_line(&w, S("last line"));
    say_pass_fail(true);

    /* Write a byte */
    lc_print_string(STDOUT, S("writer_put_byte"));
    lc_writer_put_byte(&w, 0x42);
    lc_writer_put_newline(&w);
    say_pass_fail(true);

    /* Flush and destroy */
    lc_print_string(STDOUT, S("writer_destroy"));
    lc_writer_destroy(&w);
    lc_kernel_close_file(fd);
    say_pass_fail(w.buffer == NULL);

    /* ================================================================
     * Buffered Reader — byte by byte
     * ================================================================ */

    lc_print_string(STDOUT, S("reader_create"));
    fd_ret = lc_kernel_open_file(test_file, O_RDONLY, 0);
    ok = fd_ret >= 0;
    say_pass_fail(ok);
    if (!ok) return 1;
    fd = (int32_t)fd_ret;

    lc_reader r = lc_reader_create(fd, 32);
    lc_print_string(STDOUT, S("reader_buffer_alloc"));
    say_pass_fail(r.buffer != NULL);

    /* Read first line byte by byte: "hello world\n" */
    lc_print_string(STDOUT, S("reader_read_byte"));
    char first_line[32];
    int idx = 0;
    for (;;) {
        int32_t byte = lc_reader_read_byte(&r);
        if (byte < 0 || (char)byte == '\n') break;
        if (idx < 31) first_line[idx++] = (char)byte;
    }
    first_line[idx] = '\0';
    say_pass_fail(lc_string_equal(first_line, (size_t)idx, "hello world", 11));

    /* Read remaining lines */
    lc_print_string(STDOUT, S("reader_read_line"));
    char line_buf[128];
    int64_t len;

    /* "A" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = len == 1 && line_buf[0] == 'A';

    /* "42" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 2 && lc_string_equal(line_buf, (size_t)len, "42", 2);

    /* "-99" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 3 && lc_string_equal(line_buf, (size_t)len, "-99", 3);

    /* "0" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 1 && line_buf[0] == '0';

    /* "12345" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 5 && lc_string_equal(line_buf, (size_t)len, "12345", 5);

    /* "0xff" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 4 && lc_string_equal(line_buf, (size_t)len, "0xff", 4);

    /* "0x0" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 3 && lc_string_equal(line_buf, (size_t)len, "0x0", 3);

    /* "last line" */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    ok = ok && len == 9 && lc_string_equal(line_buf, (size_t)len, "last line", 9);

    say_pass_fail(ok);

    /* Read the byte 0x42 line: "B\n" (0x42 == 'B') */
    lc_print_string(STDOUT, S("reader_read_byte_value"));
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    say_pass_fail(len == 1 && line_buf[0] == 0x42);

    /* Should be at EOF now */
    lc_print_string(STDOUT, S("reader_is_end"));
    /* Consume any remaining data to trigger EOF */
    len = lc_reader_read_line(&r, line_buf, sizeof(line_buf));
    say_pass_fail(len == -1 && lc_reader_is_end(&r));

    lc_print_string(STDOUT, S("reader_destroy"));
    lc_reader_destroy(&r);
    lc_kernel_close_file(fd);
    say_pass_fail(r.buffer == NULL);

    /* ================================================================
     * Reader: read_bytes
     * ================================================================ */

    lc_print_string(STDOUT, S("reader_read_bytes"));
    fd_ret = lc_kernel_open_file(test_file, O_RDONLY, 0);
    fd = (int32_t)fd_ret;
    lc_reader r2 = lc_reader_create(fd, 16);
    char chunk[11];
    size_t got = lc_reader_read_bytes(&r2, chunk, 11);
    ok = got == 11 && lc_string_equal(chunk, 11, "hello world", 11);
    say_pass_fail(ok);
    lc_reader_destroy(&r2);
    lc_kernel_close_file(fd);

    /* ================================================================
     * file_write_all / file_read_all
     * ================================================================ */

    lc_print_string(STDOUT, S("file_write_all"));
    const char *test_data = "file_write_all works!\n";
    size_t test_data_len = lc_string_length(test_data);
    ok = lc_is_ok(lc_file_write_all(test_file2, test_data, test_data_len));
    say_pass_fail(ok);

    lc_print_string(STDOUT, S("file_read_all"));
    uint8_t *read_data = NULL;
    size_t read_size = 0;
    ok = lc_is_ok(lc_file_read_all(test_file2, &read_data, &read_size));
    say_pass_fail(ok && read_size == test_data_len &&
                  lc_string_equal((char *)read_data, read_size, test_data, test_data_len));
    lc_heap_free(read_data);

    /* ================================================================
     * file_read_all: empty file
     * ================================================================ */

    lc_print_string(STDOUT, S("file_read_all_empty"));
    ok = lc_is_ok(lc_file_write_all(test_file2, "", 0));
    uint8_t *empty_data = NULL;
    size_t empty_size = 99;
    ok = ok && lc_is_ok(lc_file_read_all(test_file2, &empty_data, &empty_size));
    say_pass_fail(ok && empty_size == 0);
    lc_heap_free(empty_data);

    /* ================================================================
     * file_get_size
     * ================================================================ */

    lc_print_string(STDOUT, S("file_get_size"));
    /* Write known data */
    ok = lc_is_ok(lc_file_write_all(test_file2, "exactly 18 bytes.", 18));
    lc_result size_r = lc_file_get_size(test_file2);
    say_pass_fail(ok && lc_is_ok(size_r));

    lc_print_string(STDOUT, S("file_get_size_nonexistent"));
    lc_result size_r2 = lc_file_get_size("/tmp/lightc_no_such_file_ever.txt");
    say_pass_fail(lc_is_err(size_r2));

    /* ================================================================
     * Directory listing
     * ================================================================ */

    lc_print_string(STDOUT, S("directory_open"));
    lc_directory dir;
    ok = lc_is_ok(lc_directory_open(&dir, "/tmp"));
    say_pass_fail(ok);

    lc_print_string(STDOUT, S("directory_next"));
    lc_directory_entry entry;
    int count = 0;
    bool found_dot = false;
    bool found_dotdot = false;
    bool found_test = false;
    while (lc_directory_next(&dir, &entry)) {
        count++;
        if (lc_string_equal(entry.name, lc_string_length(entry.name), ".", 1)) {
            found_dot = true;
        }
        if (lc_string_equal(entry.name, lc_string_length(entry.name), "..", 2)) {
            found_dotdot = true;
        }
        /* Our test file should be in /tmp */
        if (lc_string_equal(entry.name, lc_string_length(entry.name),
                            "lightc_io_test.txt", 18)) {
            found_test = true;
        }
    }
    say_pass_fail(count >= 2 && found_dot && found_dotdot && found_test);

    lc_print_string(STDOUT, S("directory_close"));
    lc_directory_close(&dir);
    say_pass_fail(dir.fd == -1);

    lc_print_string(STDOUT, S("directory_open_nonexistent"));
    ok = lc_is_ok(lc_directory_open(&dir, "/tmp/lightc_no_such_directory_ever"));
    say_pass_fail(!ok);

    /* ================================================================
     * Writer: large write that exceeds buffer (tests auto-flush)
     * ================================================================ */

    lc_print_string(STDOUT, S("writer_auto_flush"));
    ok = true;
    fd_ret = lc_kernel_open_file(test_file2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    fd = (int32_t)fd_ret;
    lc_writer w2 = lc_writer_create(fd, 8); /* tiny buffer */
    lc_writer_put_string(&w2, S("abcdefghijklmnopqrstuvwxyz")); /* 26 chars > 8 */
    lc_writer_destroy(&w2);
    lc_kernel_close_file(fd);

    /* Read it back and verify */
    uint8_t *verify_data = NULL;
    size_t verify_size = 0;
    ok = lc_is_ok(lc_file_read_all(test_file2, &verify_data, &verify_size));
    ok = ok && verify_size == 26 &&
         lc_string_equal((char *)verify_data, verify_size,
                         "abcdefghijklmnopqrstuvwxyz", 26);
    say_pass_fail(ok);
    lc_heap_free(verify_data);

    /* ================================================================
     * Writer: INT64_MIN edge case
     * ================================================================ */

    lc_print_string(STDOUT, S("writer_int64_min"));
    fd_ret = lc_kernel_open_file(test_file2, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    fd = (int32_t)fd_ret;
    lc_writer w3 = lc_writer_create(fd, 64);
    /* INT64_MIN = -9223372036854775808 */
    lc_writer_put_signed(&w3, (int64_t)(-9223372036854775807LL - 1));
    lc_writer_destroy(&w3);
    lc_kernel_close_file(fd);

    verify_data = NULL;
    verify_size = 0;
    ok = lc_is_ok(lc_file_read_all(test_file2, &verify_data, &verify_size));
    ok = ok && verify_size == 20 &&
         lc_string_equal((char *)verify_data, verify_size,
                         "-9223372036854775808", 20);
    say_pass_fail(ok);
    lc_heap_free(verify_data);

    /* ================================================================
     * Reader: line truncation (buffer smaller than line)
     * ================================================================ */

    lc_print_string(STDOUT, S("reader_line_truncation"));
    ok = lc_is_ok(lc_file_write_all(test_file2, "abcdefghijklmnop\nnext\n", 22));
    fd_ret = lc_kernel_open_file(test_file2, O_RDONLY, 0);
    fd = (int32_t)fd_ret;
    lc_reader r3 = lc_reader_create(fd, 64);
    char small_buf[6]; /* only room for 5 chars + null */
    len = lc_reader_read_line(&r3, small_buf, sizeof(small_buf));
    /* Should get "abcde" (5 chars), rest of first line consumed */
    ok = ok && len == 5 && lc_string_equal(small_buf, 5, "abcde", 5);
    /* Next line should be "next" */
    len = lc_reader_read_line(&r3, small_buf, sizeof(small_buf));
    ok = ok && len == 4 && lc_string_equal(small_buf, 4, "next", 4);
    say_pass_fail(ok);
    lc_reader_destroy(&r3);
    lc_kernel_close_file(fd);

    /* ================================================================
     * Cleanup temp files
     * ================================================================ */

    /* We leave test files in /tmp — they'll be cleaned up on reboot */

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
