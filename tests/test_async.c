/*
 * test_async.c — tests for lightc async I/O (io_uring).
 */

#include "test.h"
#include <lightc/async.h>
#include <lightc/syscall.h>
#include <lightc/string.h>

/* ===== lc_async_ring_create / destroy ===== */

static void test_async_ring_create_destroy(void) {
    lc_result_ptr r = lc_async_ring_create(8);
    TEST_ASSERT_PTR_OK(r);
    TEST_ASSERT_NOT_NULL(r.value);

    lc_async_ring *ring = (lc_async_ring *)r.value;
    lc_async_ring_destroy(ring);
}

/* ===== Submit read/write via pipe ===== */

static void test_async_submit_read_write(void) {
    /* Create a pipe: fds[0] = read end, fds[1] = write end */
    int32_t fds[2];
    lc_sysret pipe_ret = lc_kernel_create_pipe(fds, 0);
    TEST_ASSERT(pipe_ret >= 0);

    /* Create ring */
    lc_result_ptr rp = lc_async_ring_create(8);
    TEST_ASSERT_PTR_OK(rp);
    lc_async_ring *ring = (lc_async_ring *)rp.value;

    /* Write data to the pipe synchronously */
    const char *msg = "hello async";
    size_t msg_len = lc_string_length(msg);
    lc_sysret wr = lc_kernel_write_bytes(fds[1], msg, msg_len);
    TEST_ASSERT_EQ(wr, (lc_sysret)msg_len);

    /* Submit async read from read end */
    char buf[64];
    lc_bytes_fill(buf, 0, sizeof(buf));
    lc_result sr = lc_async_submit_read(ring, fds[0], buf, (uint32_t)msg_len, (uint64_t)-1, 42);
    TEST_ASSERT_OK(sr);

    /* Flush and wait for completion */
    lc_async_result results[4];
    uint32_t n = lc_async_wait(ring, results, 4);
    TEST_ASSERT(n >= 1);

    /* Verify data matches */
    TEST_ASSERT_EQ(results[0].result, (int32_t)msg_len);
    TEST_ASSERT_STR_EQ(buf, msg_len, msg, msg_len);

    /* Cleanup */
    lc_async_ring_destroy(ring);
    lc_kernel_close_file(fds[0]);
    lc_kernel_close_file(fds[1]);
}

/* ===== Tag matching across multiple ops ===== */

static void test_async_tag_matching(void) {
    int32_t fds1[2], fds2[2];
    lc_sysret r1 = lc_kernel_create_pipe(fds1, 0);
    lc_sysret r2 = lc_kernel_create_pipe(fds2, 0);
    TEST_ASSERT(r1 >= 0);
    TEST_ASSERT(r2 >= 0);

    lc_result_ptr rp = lc_async_ring_create(8);
    TEST_ASSERT_PTR_OK(rp);
    lc_async_ring *ring = (lc_async_ring *)rp.value;

    /* Write to both pipes */
    lc_kernel_write_bytes(fds1[1], "aaa", 3);
    lc_kernel_write_bytes(fds2[1], "bbb", 3);

    /* Submit reads with different tags */
    char buf1[8], buf2[8];
    lc_bytes_fill(buf1, 0, sizeof(buf1));
    lc_bytes_fill(buf2, 0, sizeof(buf2));

    lc_result s1 = lc_async_submit_read(ring, fds1[0], buf1, 3, (uint64_t)-1, 100);
    lc_result s2 = lc_async_submit_read(ring, fds2[0], buf2, 3, (uint64_t)-1, 200);
    TEST_ASSERT_OK(s1);
    TEST_ASSERT_OK(s2);

    /* Wait for both completions */
    lc_async_result results[4];
    uint32_t total = 0;
    while (total < 2) {
        uint32_t n = lc_async_wait(ring, results + total, 4 - total);
        total += n;
    }

    /* Verify both tags appear (order may vary) */
    bool found_100 = false;
    bool found_200 = false;
    for (uint32_t i = 0; i < total; i++) {
        if (results[i].tag == 100) found_100 = true;
        if (results[i].tag == 200) found_200 = true;
    }
    TEST_ASSERT(found_100);
    TEST_ASSERT(found_200);

    lc_async_ring_destroy(ring);
    lc_kernel_close_file(fds1[0]);
    lc_kernel_close_file(fds1[1]);
    lc_kernel_close_file(fds2[0]);
    lc_kernel_close_file(fds2[1]);
}

/* ===== Peek with no pending ops returns 0 ===== */

static void test_async_peek_empty(void) {
    lc_result_ptr rp = lc_async_ring_create(8);
    TEST_ASSERT_PTR_OK(rp);
    lc_async_ring *ring = (lc_async_ring *)rp.value;

    lc_async_result results[4];
    uint32_t n = lc_async_peek(ring, results, 4);
    TEST_ASSERT_EQ(n, (uint32_t)0);

    lc_async_ring_destroy(ring);
}

/* ===== Flush returns count of submitted ops ===== */

static void test_async_flush_count(void) {
    int32_t fds[2];
    lc_sysret pr = lc_kernel_create_pipe(fds, 0);
    TEST_ASSERT(pr >= 0);

    lc_result_ptr rp = lc_async_ring_create(8);
    TEST_ASSERT_PTR_OK(rp);
    lc_async_ring *ring = (lc_async_ring *)rp.value;

    /* Write some data so reads will complete */
    lc_kernel_write_bytes(fds[1], "xxxxxx", 6);

    /* Submit 3 reads */
    char buf1[4], buf2[4], buf3[4];
    lc_result s1 = lc_async_submit_read(ring, fds[0], buf1, 2, (uint64_t)-1, 1);
    lc_result s2 = lc_async_submit_read(ring, fds[0], buf2, 2, (uint64_t)-1, 2);
    lc_result s3 = lc_async_submit_read(ring, fds[0], buf3, 2, (uint64_t)-1, 3);
    TEST_ASSERT_OK(s1);
    TEST_ASSERT_OK(s2);
    TEST_ASSERT_OK(s3);

    uint32_t flushed = lc_async_flush(ring);
    TEST_ASSERT_EQ(flushed, (uint32_t)3);

    /* Drain completions */
    lc_async_result results[4];
    uint32_t total = 0;
    while (total < 3) {
        uint32_t n = lc_async_wait(ring, results + total, 4 - total);
        total += n;
    }

    lc_async_ring_destroy(ring);
    lc_kernel_close_file(fds[0]);
    lc_kernel_close_file(fds[1]);
}

/* ===== Free slots decrease after submissions ===== */

static void test_async_free_slots(void) {
    lc_result_ptr rp = lc_async_ring_create(8);
    TEST_ASSERT_PTR_OK(rp);
    lc_async_ring *ring = (lc_async_ring *)rp.value;

    uint32_t initial_slots = lc_async_get_free_slots(ring);
    TEST_ASSERT(initial_slots > 0);

    /* Create a pipe for a valid fd target */
    int32_t fds[2];
    lc_sysret pr = lc_kernel_create_pipe(fds, 0);
    TEST_ASSERT(pr >= 0);

    /* Write data so read can succeed */
    lc_kernel_write_bytes(fds[1], "xx", 2);

    /* Submit one op */
    char buf[4];
    lc_result sr = lc_async_submit_read(ring, fds[0], buf, 2, (uint64_t)-1, 0);
    TEST_ASSERT_OK(sr);

    uint32_t after_slots = lc_async_get_free_slots(ring);
    TEST_ASSERT(after_slots < initial_slots);

    /* Flush and drain to leave ring clean */
    lc_async_result results[4];
    lc_async_wait(ring, results, 4);

    lc_async_ring_destroy(ring);
    lc_kernel_close_file(fds[0]);
    lc_kernel_close_file(fds[1]);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Check if io_uring is available — skip all tests if not */
    {
        lc_result_ptr probe = lc_async_ring_create(4);
        if (lc_ptr_is_err(probe)) {
            _test_print_text("SKIP: io_uring not available (kernel 5.1+ required)\n");
            _test_print_text("\n========================================\n");
            _test_print_text("Tests run:    0\nTests passed: 0\nTests failed: 0\n");
            _test_print_text("========================================\nRESULT: PASS\n");
            return 0;
        }
        lc_async_ring_destroy(probe.value);
    }

    /* ring create/destroy */
    TEST_RUN(test_async_ring_create_destroy);

    /* submit read/write via pipe */
    TEST_RUN(test_async_submit_read_write);

    /* tag matching across multiple ops */
    TEST_RUN(test_async_tag_matching);

    /* peek with no pending ops */
    TEST_RUN(test_async_peek_empty);

    /* flush returns submitted count */
    TEST_RUN(test_async_flush_count);

    /* free slots decrease after submissions */
    TEST_RUN(test_async_free_slots);

    return test_main();
}
