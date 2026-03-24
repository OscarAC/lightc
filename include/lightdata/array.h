#ifndef LIGHTDATA_ARRAY_H
#define LIGHTDATA_ARRAY_H

#include <lightc/types.h>

/*
 * Dynamic array — contiguous growable buffer.
 * Cache-friendly: elements stored contiguously for sequential access.
 * Geometric growth (2x) for amortized O(1) push.
 *
 *   lc_array arr = lc_array_create(sizeof(int32_t));
 *   int32_t val = 42;
 *   lc_array_push(&arr, &val);
 *   int32_t *p = lc_array_get(&arr, 0);  // → 42
 *   lc_array_destroy(&arr);
 */

typedef struct {
    uint8_t *data;          /* heap-allocated buffer */
    size_t   element_size;  /* size of each element */
    size_t   count;         /* number of elements */
    size_t   capacity;      /* allocated capacity (elements) */
} lc_array;

lc_array lc_array_create(size_t element_size);
void lc_array_destroy(lc_array *arr);

/* Add element to the end. value = pointer to the new element. */
[[nodiscard]] lc_result_ptr lc_array_push(lc_array *arr, const void *element);

/* Remove and copy last element. Returns false if empty. */
bool lc_array_pop(lc_array *arr, void *out);

/* Get pointer to element at index. No bounds check. */
void *lc_array_get(const lc_array *arr, size_t index);

/* Get number of elements. */
size_t lc_array_count(const lc_array *arr);

/* Is the array empty? */
bool lc_array_is_empty(const lc_array *arr);

/* Remove all elements (keeps allocated memory). */
void lc_array_clear(lc_array *arr);

/* Ensure capacity for at least `min_capacity` elements. */
[[nodiscard]] lc_result lc_array_reserve(lc_array *arr, size_t min_capacity);

/* Insert element at index, shifting others right. value = pointer to inserted element. */
[[nodiscard]] lc_result_ptr lc_array_insert(lc_array *arr, size_t index, const void *element);

/* Remove element at index, shifting others left. */
void lc_array_remove(lc_array *arr, size_t index);

/* Get pointer to underlying data. */
void *lc_array_data(const lc_array *arr);

#endif
