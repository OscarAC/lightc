#ifndef LIGHTDATA_RINGBUF_H
#define LIGHTDATA_RINGBUF_H

#include <lightc/types.h>

/*
 * Ring buffer — fixed-size circular buffer.
 * Lock-free for single-producer single-consumer (SPSC).
 * Great for event queues, I/O buffers, producer-consumer patterns.
 * Capacity must be a power of 2 (for fast modulo via bitmask).
 *
 *   lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 1024);
 *   int32_t val = 42;
 *   lc_ringbuf_push(&ring, &val);
 *   lc_ringbuf_pop(&ring, &val);  // val == 42
 *   lc_ringbuf_destroy(&ring);
 */

typedef struct {
    uint8_t *data;
    size_t   element_size;
    size_t   capacity;      /* always power of 2 */
    size_t   mask;           /* capacity - 1, for fast modulo */
    size_t   head;           /* read position */
    size_t   tail;           /* write position */
} lc_ringbuf;

/* Create ring buffer. Capacity is rounded up to next power of 2. */
lc_ringbuf lc_ringbuf_create(size_t element_size, size_t min_capacity);
void lc_ringbuf_destroy(lc_ringbuf *ring);

/* Push element to tail. error = LC_ERR_FULL if full. */
[[nodiscard]] lc_result lc_ringbuf_push(lc_ringbuf *ring, const void *element);

/* Pop element from head. Returns false if empty. */
bool lc_ringbuf_pop(lc_ringbuf *ring, void *out);

/* Peek at head without removing. Returns NULL if empty. */
void *lc_ringbuf_peek(const lc_ringbuf *ring);

/* Number of elements currently in the buffer. */
size_t lc_ringbuf_count(const lc_ringbuf *ring);

/* Is the buffer empty? */
bool lc_ringbuf_is_empty(const lc_ringbuf *ring);

/* Is the buffer full? */
bool lc_ringbuf_is_full(const lc_ringbuf *ring);

/* Remove all elements. */
void lc_ringbuf_clear(lc_ringbuf *ring);

#endif
