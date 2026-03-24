#include <lightc/async.h>
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/heap.h>

/* ========================================================================
 * io_uring kernel structures
 *
 * Defined here — not in the header — because users don't need them.
 * These match the Linux UAPI exactly.
 * ======================================================================== */

/* Submission Queue Entry — 64 bytes */
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t ioprio;
    int32_t  fd;
    uint64_t off;
    uint64_t addr;
    uint32_t len;
    uint32_t rw_flags;
    uint64_t user_data;
    uint16_t buf_index;
    uint16_t personality;
    int32_t  splice_fd_in;
    uint64_t _pad[2];
} io_uring_sqe;
_Static_assert(sizeof(io_uring_sqe) == 64, "SQE must be 64 bytes");

/* Completion Queue Entry — 16 bytes */
typedef struct {
    uint64_t user_data;
    int32_t  res;
    uint32_t flags;
} io_uring_cqe;
_Static_assert(sizeof(io_uring_cqe) == 16, "CQE must be 16 bytes");

/* Offsets into the mmap'd SQ ring */
typedef struct {
    uint32_t head, tail, ring_mask, ring_entries;
    uint32_t flags, dropped, array, resv1;
    uint64_t resv2;
} io_sqring_offsets;

/* Offsets into the mmap'd CQ ring */
typedef struct {
    uint32_t head, tail, ring_mask, ring_entries;
    uint32_t overflow, cqes, flags, resv1;
    uint64_t resv2;
} io_cqring_offsets;

/* Parameters for io_uring_setup */
typedef struct {
    uint32_t sq_entries, cq_entries, flags;
    uint32_t sq_thread_cpu, sq_thread_idle;
    uint32_t features, wq_fd;
    uint32_t resv[3];
    io_sqring_offsets sq_off;
    io_cqring_offsets cq_off;
} io_uring_params;

/* ========================================================================
 * Constants
 * ======================================================================== */

#define IORING_OFF_SQ_RING  0ULL
#define IORING_OFF_CQ_RING  0x8000000ULL
#define IORING_OFF_SQES     0x10000000ULL

#define IORING_OP_READ   22
#define IORING_OP_WRITE  23

#define IORING_ENTER_GETEVENTS  (1U << 0)

#define IORING_FEAT_SINGLE_MMAP  (1U << 0)

#define MAP_SHARED    0x01
#define MAP_POPULATE  0x008000

/* ========================================================================
 * Ring structure
 * ======================================================================== */

struct lc_async_ring {
    int32_t fd;

    /* SQ ring pointers (into mmap'd memory) */
    uint32_t *sq_head;
    uint32_t *sq_tail;
    uint32_t *sq_ring_mask;
    uint32_t *sq_ring_entries;
    uint32_t *sq_flags;
    uint32_t *sq_array;
    io_uring_sqe *sqes;

    /* CQ ring pointers */
    uint32_t *cq_head;
    uint32_t *cq_tail;
    uint32_t *cq_ring_mask;
    uint32_t *cq_ring_entries;
    io_uring_cqe *cqes;

    /* mmap tracking for cleanup */
    void   *sq_ring_ptr;
    size_t  sq_ring_size;
    void   *cq_ring_ptr;
    size_t  cq_ring_size;
    void   *sqes_ptr;
    size_t  sqes_size;

    /* Local submission tracking */
    uint32_t sq_pending;  /* entries written but not submitted */
};

/* ========================================================================
 * Atomic helpers for shared memory ring buffers
 * ======================================================================== */

static inline uint32_t load_acquire(const uint32_t *p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void store_release(uint32_t *p, uint32_t v) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

/* ========================================================================
 * Internal: reap completions from the CQ ring
 * ======================================================================== */

static uint32_t reap_completions(lc_async_ring *ring,
                                 lc_async_result *results, uint32_t max_results) {
    uint32_t head  = load_acquire(ring->cq_head);
    uint32_t tail  = load_acquire(ring->cq_tail);
    uint32_t count = 0;

    while (head != tail && count < max_results) {
        io_uring_cqe *cqe = &ring->cqes[head & *ring->cq_ring_mask];
        results[count].tag    = cqe->user_data;
        results[count].result = cqe->res;
        count++;
        head++;
    }

    store_release(ring->cq_head, head);
    return count;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

lc_result_ptr lc_async_ring_create(uint32_t queue_size) {
    io_uring_params params;
    lc_bytes_fill(&params, 0, sizeof(params));

    /* Ask the kernel to create an io_uring instance */
    lc_sysret ret = lc_kernel_io_ring_setup(queue_size, &params);
    if (ret < 0) return lc_err_ptr((int32_t)(-ret));
    int32_t ring_fd = (int32_t)ret;

    /* Calculate mmap sizes */
    size_t sq_ring_size = params.sq_off.array + params.sq_entries * sizeof(uint32_t);
    size_t cq_ring_size = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);
    size_t sqes_size    = params.sq_entries * sizeof(io_uring_sqe);

    /* If kernel supports single mmap, the SQ and CQ rings share one mapping.
     * Use the larger of the two sizes. */
    bool single_mmap = (params.features & IORING_FEAT_SINGLE_MMAP) != 0;
    if (single_mmap) {
        if (cq_ring_size > sq_ring_size) {
            sq_ring_size = cq_ring_size;
        }
        cq_ring_size = sq_ring_size;
    }

    /* mmap the SQ ring */
    void *sq_ring_ptr = lc_kernel_map_memory(
        NULL, sq_ring_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        ring_fd, (int64_t)IORING_OFF_SQ_RING
    );
    if (sq_ring_ptr == MAP_FAILED) {
        lc_kernel_close_file(ring_fd);
        return lc_err_ptr(LC_ERR_NOMEM);
    }

    /* mmap the CQ ring (or reuse SQ mapping if single mmap) */
    void *cq_ring_ptr;
    if (single_mmap) {
        cq_ring_ptr = sq_ring_ptr;
    } else {
        cq_ring_ptr = lc_kernel_map_memory(
            NULL, cq_ring_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_POPULATE,
            ring_fd, (int64_t)IORING_OFF_CQ_RING
        );
        if (cq_ring_ptr == MAP_FAILED) {
            lc_kernel_unmap_memory(sq_ring_ptr, sq_ring_size);
            lc_kernel_close_file(ring_fd);
            return lc_err_ptr(LC_ERR_NOMEM);
        }
    }

    /* mmap the SQE array */
    void *sqes_ptr = lc_kernel_map_memory(
        NULL, sqes_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_POPULATE,
        ring_fd, (int64_t)IORING_OFF_SQES
    );
    if (sqes_ptr == MAP_FAILED) {
        lc_kernel_unmap_memory(sq_ring_ptr, sq_ring_size);
        if (!single_mmap) {
            lc_kernel_unmap_memory(cq_ring_ptr, cq_ring_size);
        }
        lc_kernel_close_file(ring_fd);
        return lc_err_ptr(LC_ERR_NOMEM);
    }

    /* Allocate the ring struct */
    lc_result_ptr alloc = lc_heap_allocate(sizeof(lc_async_ring));
    if (lc_ptr_is_err(alloc)) {
        lc_kernel_unmap_memory(sqes_ptr, sqes_size);
        lc_kernel_unmap_memory(sq_ring_ptr, sq_ring_size);
        if (!single_mmap) {
            lc_kernel_unmap_memory(cq_ring_ptr, cq_ring_size);
        }
        lc_kernel_close_file(ring_fd);
        return lc_err_ptr(LC_ERR_NOMEM);
    }
    lc_async_ring *ring = alloc.value;

    ring->fd = ring_fd;

    /* Set up SQ ring pointers using offsets from params */
    uint8_t *sq_base = (uint8_t *)sq_ring_ptr;
    ring->sq_head         = (uint32_t *)(sq_base + params.sq_off.head);
    ring->sq_tail         = (uint32_t *)(sq_base + params.sq_off.tail);
    ring->sq_ring_mask    = (uint32_t *)(sq_base + params.sq_off.ring_mask);
    ring->sq_ring_entries = (uint32_t *)(sq_base + params.sq_off.ring_entries);
    ring->sq_flags        = (uint32_t *)(sq_base + params.sq_off.flags);
    ring->sq_array        = (uint32_t *)(sq_base + params.sq_off.array);
    ring->sqes            = (io_uring_sqe *)sqes_ptr;

    /* Set up CQ ring pointers */
    uint8_t *cq_base = (uint8_t *)cq_ring_ptr;
    ring->cq_head         = (uint32_t *)(cq_base + params.cq_off.head);
    ring->cq_tail         = (uint32_t *)(cq_base + params.cq_off.tail);
    ring->cq_ring_mask    = (uint32_t *)(cq_base + params.cq_off.ring_mask);
    ring->cq_ring_entries = (uint32_t *)(cq_base + params.cq_off.ring_entries);
    ring->cqes            = (io_uring_cqe *)(cq_base + params.cq_off.cqes);

    /* Track mmap regions for cleanup */
    ring->sq_ring_ptr  = sq_ring_ptr;
    ring->sq_ring_size = sq_ring_size;
    ring->cq_ring_ptr  = cq_ring_ptr;
    ring->cq_ring_size = cq_ring_size;
    ring->sqes_ptr     = sqes_ptr;
    ring->sqes_size    = sqes_size;

    ring->sq_pending = 0;

    /* Initialize sq_array to identity mapping: sq_array[i] = i */
    for (uint32_t i = 0; i < params.sq_entries; i++) {
        ring->sq_array[i] = i;
    }

    return lc_ok_ptr(ring);
}

void lc_async_ring_destroy(lc_async_ring *ring) {
    if (ring == NULL) return;

    /* Unmap SQE array */
    lc_kernel_unmap_memory(ring->sqes_ptr, ring->sqes_size);

    /* Unmap CQ ring (only if it's a separate mapping) */
    if (ring->cq_ring_ptr != ring->sq_ring_ptr) {
        lc_kernel_unmap_memory(ring->cq_ring_ptr, ring->cq_ring_size);
    }

    /* Unmap SQ ring */
    lc_kernel_unmap_memory(ring->sq_ring_ptr, ring->sq_ring_size);

    /* Close the ring fd */
    lc_kernel_close_file(ring->fd);

    /* Free the struct */
    lc_heap_free(ring);
}

/* Internal: prepare an SQE for a read or write operation */
static lc_result submit_rw(lc_async_ring *ring, uint8_t opcode, int32_t fd,
                           uint64_t addr, uint32_t len, uint64_t offset, uint64_t tag) {
    /* Check if SQ is full */
    uint32_t tail    = load_acquire(ring->sq_tail);
    uint32_t head    = load_acquire(ring->sq_head);
    uint32_t entries = *ring->sq_ring_entries;
    uint32_t mask    = *ring->sq_ring_mask;

    if (tail + ring->sq_pending - head >= entries) {
        return lc_err(LC_ERR_AGAIN);  /* queue is full */
    }

    /* Get the next SQE slot */
    uint32_t index = (tail + ring->sq_pending) & mask;
    io_uring_sqe *sqe = &ring->sqes[index];

    /* Zero the SQE */
    lc_bytes_fill(sqe, 0, sizeof(*sqe));

    /* Fill in the operation */
    sqe->opcode    = opcode;
    sqe->fd        = fd;
    sqe->addr      = addr;
    sqe->len       = len;
    sqe->off       = offset;
    sqe->user_data = tag;

    /* Update the sq_array entry */
    ring->sq_array[(tail + ring->sq_pending) & mask] = index;

    ring->sq_pending++;
    return lc_ok(0);
}

lc_result lc_async_submit_read(lc_async_ring *ring, int32_t fd,
                               void *buf, uint32_t count,
                               uint64_t offset, uint64_t tag) {
    return submit_rw(ring, IORING_OP_READ, fd,
                     (uint64_t)(uintptr_t)buf, count, offset, tag);
}

lc_result lc_async_submit_write(lc_async_ring *ring, int32_t fd,
                                const void *buf, uint32_t count,
                                uint64_t offset, uint64_t tag) {
    return submit_rw(ring, IORING_OP_WRITE, fd,
                     (uint64_t)(uintptr_t)buf, count, offset, tag);
}

uint32_t lc_async_flush(lc_async_ring *ring) {
    if (ring->sq_pending == 0) return 0;

    uint32_t to_submit = ring->sq_pending;

    /* Make the new SQEs visible to the kernel */
    store_release(ring->sq_tail, *ring->sq_tail + to_submit);

    /* Tell the kernel to process them */
    lc_sysret ret = lc_kernel_io_ring_enter(ring->fd, to_submit, 0, 0);

    if (ret < 0) return 0;  /* don't clear sq_pending on failure */

    ring->sq_pending = 0;
    return (uint32_t)ret;
}

uint32_t lc_async_wait(lc_async_ring *ring,
                       lc_async_result *results, uint32_t max_results) {
    /* Flush any pending submissions first */
    lc_async_flush(ring);

    /* Wait for at least one completion, retrying on EINTR */
    lc_sysret ret;
    do {
        ret = lc_kernel_io_ring_enter(ring->fd, 0, 1, IORING_ENTER_GETEVENTS);
    } while (ret == -4); /* -4 = EINTR */

    return reap_completions(ring, results, max_results);
}

uint32_t lc_async_peek(lc_async_ring *ring,
                       lc_async_result *results, uint32_t max_results) {
    return reap_completions(ring, results, max_results);
}

lc_result lc_async_submit_raw(lc_async_ring *ring, uint8_t opcode, int32_t fd,
                              uint64_t addr, uint32_t len, uint64_t offset,
                              uint32_t op_flags, uint64_t tag) {
    /* Check if SQ is full */
    uint32_t tail    = load_acquire(ring->sq_tail);
    uint32_t head    = load_acquire(ring->sq_head);
    uint32_t entries = *ring->sq_ring_entries;
    uint32_t mask    = *ring->sq_ring_mask;

    if (tail + ring->sq_pending - head >= entries) {
        return lc_err(LC_ERR_AGAIN);  /* queue is full */
    }

    /* Get the next SQE slot */
    uint32_t index = (tail + ring->sq_pending) & mask;
    io_uring_sqe *sqe = &ring->sqes[index];

    /* Zero the SQE */
    lc_bytes_fill(sqe, 0, sizeof(*sqe));

    /* Fill in all fields */
    sqe->opcode    = opcode;
    sqe->fd        = fd;
    sqe->addr      = addr;
    sqe->len       = len;
    sqe->off       = offset;
    sqe->rw_flags  = op_flags;
    sqe->user_data = tag;

    /* Update the sq_array entry */
    ring->sq_array[(tail + ring->sq_pending) & mask] = index;

    ring->sq_pending++;
    return lc_ok(0);
}

uint32_t lc_async_get_free_slots(const lc_async_ring *ring) {
    uint32_t entries = *ring->sq_ring_entries;
    uint32_t tail    = load_acquire(ring->sq_tail);
    uint32_t head    = load_acquire(ring->sq_head);
    uint32_t used    = tail - head + ring->sq_pending;
    if (used >= entries) return 0;
    return entries - used;
}
