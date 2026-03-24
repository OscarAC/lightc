/*
 * Exercise the async I/O layer (io_uring): create ring, submit reads/writes,
 * flush, wait, peek, batch operations, get_free_slots, sequential offset.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/heap.h>
#include <lightc/io.h>
#include <lightc/async.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) {
        lc_print_line(STDOUT, S(" PASS"));
    } else {
        lc_print_line(STDOUT, S(" FAIL"));
    }
}

static const char *test_read_file  = "/tmp/lightc_async_read.txt";
static const char *test_write_file = "/tmp/lightc_async_write.txt";
static const char *test_batch_file = "/tmp/lightc_async_batch.txt";
static const char *test_peek_file  = "/tmp/lightc_async_peek.txt";
static const char *test_seq_file   = "/tmp/lightc_async_seq.txt";

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    bool ok;

    /* ================================================================
     * Create a ring
     * ================================================================ */

    lc_print_string(STDOUT, S("ring_create"));
    lc_result_ptr ring_r = lc_async_ring_create(8);
    lc_async_ring *ring = ring_r.value;
    ok = !lc_ptr_is_err(ring_r);
    say_pass_fail(ok);
    if (!ok) return 1;

    /* ================================================================
     * get_free_slots on a fresh ring
     * ================================================================ */

    lc_print_string(STDOUT, S("get_free_slots_initial"));
    uint32_t free_slots = lc_async_get_free_slots(ring);
    /* Queue depth is at least 8 (kernel may round up to power of 2) */
    ok = free_slots >= 8;
    say_pass_fail(ok);

    /* ================================================================
     * Async read: write a file with sync I/O, then read it back async
     * ================================================================ */

    lc_print_string(STDOUT, S("async_read"));
    const char *read_test_data = "async read works!";
    size_t read_test_len = 17;
    ok = lc_is_ok(lc_file_write_all(test_read_file, read_test_data, read_test_len));
    if (!ok) { say_pass_fail(false); return 1; }

    lc_sysret fd_ret = lc_kernel_open_file(test_read_file, O_RDONLY, 0);
    if (fd_ret < 0) { say_pass_fail(false); return 1; }
    int32_t read_fd = (int32_t)fd_ret;

    char read_buf[64];
    lc_bytes_fill(read_buf, 0, sizeof(read_buf));
    ok = lc_is_ok(lc_async_submit_read(ring, read_fd, read_buf, (uint32_t)read_test_len, 0, 100));
    if (!ok) { say_pass_fail(false); return 1; }

    lc_async_result results[8];
    uint32_t completed = lc_async_wait(ring, results, 8);
    ok = completed == 1 &&
         results[0].tag == 100 &&
         results[0].result == (int32_t)read_test_len &&
         lc_string_equal(read_buf, read_test_len, read_test_data, read_test_len);
    say_pass_fail(ok);
    lc_kernel_close_file(read_fd);

    /* ================================================================
     * Async write: write to a new file async, then read back with sync
     * ================================================================ */

    lc_print_string(STDOUT, S("async_write"));
    fd_ret = lc_kernel_open_file(test_write_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_ret < 0) { say_pass_fail(false); return 1; }
    int32_t write_fd = (int32_t)fd_ret;

    const char *write_test_data = "async write works!";
    size_t write_test_len = 18;
    ok = lc_is_ok(lc_async_submit_write(ring, write_fd, write_test_data,
                               (uint32_t)write_test_len, 0, 200));
    if (!ok) { say_pass_fail(false); return 1; }

    completed = lc_async_wait(ring, results, 8);
    ok = completed == 1 &&
         results[0].tag == 200 &&
         results[0].result == (int32_t)write_test_len;
    lc_kernel_close_file(write_fd);

    /* Read back with sync I/O and verify */
    if (ok) {
        uint8_t *verify_data = NULL;
        size_t verify_size = 0;
        ok = lc_is_ok(lc_file_read_all(test_write_file, &verify_data, &verify_size));
        ok = ok && verify_size == write_test_len &&
             lc_string_equal((char *)verify_data, verify_size,
                             write_test_data, write_test_len);
        lc_heap_free(verify_data);
    }
    say_pass_fail(ok);

    /* ================================================================
     * Batch: submit multiple reads, wait for all
     * ================================================================ */

    lc_print_string(STDOUT, S("async_batch"));

    /* Write a file with 4 distinct 8-byte chunks */
    const char *batch_data = "AAAAAAA\nBBBBBBB\nCCCCCCC\nDDDDDDD\n";
    size_t batch_data_len = 32;
    ok = lc_is_ok(lc_file_write_all(test_batch_file, batch_data, batch_data_len));
    if (!ok) { say_pass_fail(false); return 1; }

    fd_ret = lc_kernel_open_file(test_batch_file, O_RDONLY, 0);
    if (fd_ret < 0) { say_pass_fail(false); return 1; }
    int32_t batch_fd = (int32_t)fd_ret;

    char batch_buf0[8], batch_buf1[8], batch_buf2[8], batch_buf3[8];
    lc_bytes_fill(batch_buf0, 0, 8);
    lc_bytes_fill(batch_buf1, 0, 8);
    lc_bytes_fill(batch_buf2, 0, 8);
    lc_bytes_fill(batch_buf3, 0, 8);

    ok = lc_is_ok(lc_async_submit_read(ring, batch_fd, batch_buf0, 8, 0, 300));
    ok = ok && lc_is_ok(lc_async_submit_read(ring, batch_fd, batch_buf1, 8, 8, 301));
    ok = ok && lc_is_ok(lc_async_submit_read(ring, batch_fd, batch_buf2, 8, 16, 302));
    ok = ok && lc_is_ok(lc_async_submit_read(ring, batch_fd, batch_buf3, 8, 24, 303));
    if (!ok) { say_pass_fail(false); return 1; }

    /* Wait and collect all completions */
    uint32_t total_completed = 0;
    lc_async_result batch_results[8];
    while (total_completed < 4) {
        completed = lc_async_wait(ring, batch_results + total_completed,
                                  8 - total_completed);
        total_completed += completed;
    }

    ok = total_completed == 4;

    /* Verify each chunk (completions may arrive in any order) */
    bool got[4] = {false, false, false, false};
    for (uint32_t i = 0; i < total_completed; i++) {
        uint64_t t = batch_results[i].tag;
        if (t >= 300 && t <= 303) {
            got[t - 300] = true;
            ok = ok && batch_results[i].result == 8;
        } else {
            ok = false;
        }
    }
    ok = ok && got[0] && got[1] && got[2] && got[3];

    /* Verify buffer contents */
    ok = ok && lc_string_equal(batch_buf0, 8, "AAAAAAA\n", 8);
    ok = ok && lc_string_equal(batch_buf1, 8, "BBBBBBB\n", 8);
    ok = ok && lc_string_equal(batch_buf2, 8, "CCCCCCC\n", 8);
    ok = ok && lc_string_equal(batch_buf3, 8, "DDDDDDD\n", 8);
    say_pass_fail(ok);

    lc_kernel_close_file(batch_fd);

    /* ================================================================
     * Peek: submit, then poll without blocking until complete
     * ================================================================ */

    lc_print_string(STDOUT, S("async_peek"));
    ok = lc_is_ok(lc_file_write_all(test_peek_file, "peek test!", 10));
    if (!ok) { say_pass_fail(false); return 1; }

    fd_ret = lc_kernel_open_file(test_peek_file, O_RDONLY, 0);
    if (fd_ret < 0) { say_pass_fail(false); return 1; }
    int32_t peek_fd = (int32_t)fd_ret;

    char peek_buf[16];
    lc_bytes_fill(peek_buf, 0, sizeof(peek_buf));
    ok = lc_is_ok(lc_async_submit_read(ring, peek_fd, peek_buf, 10, 0, 400));
    if (!ok) { say_pass_fail(false); return 1; }

    /* Flush to kernel so the operation actually runs */
    lc_async_flush(ring);

    /* Poll until we get the completion */
    uint32_t peek_count = 0;
    for (int attempts = 0; attempts < 100000; attempts++) {
        peek_count = lc_async_peek(ring, results, 8);
        if (peek_count > 0) break;
    }

    ok = peek_count == 1 &&
         results[0].tag == 400 &&
         results[0].result == 10 &&
         lc_string_equal(peek_buf, 10, "peek test!", 10);
    say_pass_fail(ok);
    lc_kernel_close_file(peek_fd);

    /* ================================================================
     * get_free_slots: verify it decreases with pending submissions
     * ================================================================ */

    lc_print_string(STDOUT, S("get_free_slots_pending"));
    uint32_t initial_free = lc_async_get_free_slots(ring);

    /* Write a dummy file for reading */
    ok = lc_is_ok(lc_file_write_all(test_read_file, "slots", 5));
    fd_ret = lc_kernel_open_file(test_read_file, O_RDONLY, 0);
    if (fd_ret < 0 || !ok) { say_pass_fail(false); return 1; }
    int32_t slots_fd = (int32_t)fd_ret;

    char slots_buf[8];
    ok = lc_is_ok(lc_async_submit_read(ring, slots_fd, slots_buf, 5, 0, 500));
    uint32_t after_one = lc_async_get_free_slots(ring);
    ok = ok && (after_one == initial_free - 1);

    /* Flush and wait to clean up */
    completed = lc_async_wait(ring, results, 8);
    ok = ok && completed == 1;
    say_pass_fail(ok);
    lc_kernel_close_file(slots_fd);

    /* ================================================================
     * Sequential reads with offset -1 (current file position)
     * ================================================================ */

    lc_print_string(STDOUT, S("async_sequential_offset"));
    const char *seq_data = "firstsecondthird";
    size_t seq_data_len = 16;
    ok = lc_is_ok(lc_file_write_all(test_seq_file, seq_data, seq_data_len));
    if (!ok) { say_pass_fail(false); return 1; }

    fd_ret = lc_kernel_open_file(test_seq_file, O_RDONLY, 0);
    if (fd_ret < 0) { say_pass_fail(false); return 1; }
    int32_t seq_fd = (int32_t)fd_ret;

    /* Read "first" (5 bytes) at current position (0) */
    char seq_buf1[8];
    lc_bytes_fill(seq_buf1, 0, sizeof(seq_buf1));
    ok = lc_is_ok(lc_async_submit_read(ring, seq_fd, seq_buf1, 5, (uint64_t)-1, 600));
    if (!ok) { say_pass_fail(false); return 1; }
    completed = lc_async_wait(ring, results, 8);
    ok = completed == 1 &&
         results[0].tag == 600 &&
         results[0].result == 5 &&
         lc_string_equal(seq_buf1, 5, "first", 5);

    /* Read "second" (6 bytes) at current position (should be 5 now) */
    char seq_buf2[8];
    lc_bytes_fill(seq_buf2, 0, sizeof(seq_buf2));
    if (ok) {
        ok = lc_is_ok(lc_async_submit_read(ring, seq_fd, seq_buf2, 6, (uint64_t)-1, 601));
        if (!ok) { say_pass_fail(false); return 1; }
        completed = lc_async_wait(ring, results, 8);
        ok = completed == 1 &&
             results[0].tag == 601 &&
             results[0].result == 6 &&
             lc_string_equal(seq_buf2, 6, "second", 6);
    }

    /* Read "third" (5 bytes) at current position (should be 11 now) */
    char seq_buf3[8];
    lc_bytes_fill(seq_buf3, 0, sizeof(seq_buf3));
    if (ok) {
        ok = lc_is_ok(lc_async_submit_read(ring, seq_fd, seq_buf3, 5, (uint64_t)-1, 602));
        if (!ok) { say_pass_fail(false); return 1; }
        completed = lc_async_wait(ring, results, 8);
        ok = completed == 1 &&
             results[0].tag == 602 &&
             results[0].result == 5 &&
             lc_string_equal(seq_buf3, 5, "third", 5);
    }

    say_pass_fail(ok);
    lc_kernel_close_file(seq_fd);

    /* ================================================================
     * Destroy ring
     * ================================================================ */

    lc_print_string(STDOUT, S("ring_destroy"));
    lc_async_ring_destroy(ring);
    say_pass_fail(true);

    /* ================================================================
     * Destroy NULL ring (should not crash)
     * ================================================================ */

    lc_print_string(STDOUT, S("ring_destroy_null"));
    lc_async_ring_destroy(NULL);
    say_pass_fail(true);

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
