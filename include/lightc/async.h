#ifndef LIGHTC_ASYNC_H
#define LIGHTC_ASYNC_H

#include "types.h"

/*
 * Async I/O — built on io_uring.
 *
 * io_uring uses shared memory ring buffers between user and kernel.
 * After setup, I/O can be submitted and completed with zero syscalls
 * on the fast path.
 *
 *   1. Create a ring: lc_async_ring_create(queue_size)
 *   2. Submit work:   lc_async_submit_read/write(ring, ...)
 *   3. Flush to kernel: lc_async_flush(ring) — or automatic on wait
 *   4. Get results:   lc_async_wait(ring, ...) or lc_async_peek(ring, ...)
 *   5. Destroy:       lc_async_ring_destroy(ring)
 */

/* Result of a completed async operation */
typedef struct {
    uint64_t tag;       /* user-provided tag from submit */
    int32_t  result;    /* bytes transferred, or negative errno */
} lc_async_result;

/* Opaque ring structure — see async.c for internals */
typedef struct lc_async_ring lc_async_ring;

/* Create an async I/O ring with the given queue depth.
 * Returns NULL on failure. Caller must destroy with lc_async_ring_destroy. */
lc_async_ring *lc_async_ring_create(uint32_t queue_size);

/* Destroy the ring and free all resources. */
void lc_async_ring_destroy(lc_async_ring *ring);

/* Queue a read operation. Does not submit to kernel yet.
 * tag: user-defined value returned in the completion result.
 * offset: file offset (-1 for current position). */
bool lc_async_submit_read(lc_async_ring *ring, int32_t fd,
                          void *buf, uint32_t count,
                          uint64_t offset, uint64_t tag);

/* Queue a write operation. Does not submit to kernel yet. */
bool lc_async_submit_write(lc_async_ring *ring, int32_t fd,
                           const void *buf, uint32_t count,
                           uint64_t offset, uint64_t tag);

/* Submit all queued operations to the kernel.
 * Returns number of operations submitted. */
uint32_t lc_async_flush(lc_async_ring *ring);

/* Submit pending ops and wait for at least one completion.
 * Returns number of completions written to results (up to max_results). */
uint32_t lc_async_wait(lc_async_ring *ring,
                       lc_async_result *results, uint32_t max_results);

/* Check for completions without blocking.
 * Returns number of completions written to results (0 if none ready). */
uint32_t lc_async_peek(lc_async_ring *ring,
                       lc_async_result *results, uint32_t max_results);

/* Get how many submission slots are available. */
uint32_t lc_async_get_free_slots(const lc_async_ring *ring);

/* Submit a raw SQE with explicit opcode and parameters.
 * Used by lightio for accept, timeout, and other operations. */
bool lc_async_submit_raw(lc_async_ring *ring, uint8_t opcode, int32_t fd,
                         uint64_t addr, uint32_t len, uint64_t offset,
                         uint32_t op_flags, uint64_t tag);

#endif /* LIGHTC_ASYNC_H */
