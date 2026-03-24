/*
 * containers.c — implementations for array, hashmap, ringbuf, list.
 */

#include <lightdata/array.h>
#include <lightdata/hashmap.h>
#include <lightdata/ringbuf.h>
#include <lightdata/list.h>
#include <lightc/heap.h>
#include <lightc/string.h>

/* ========================================================================
 * Helpers
 * ======================================================================== */

static size_t next_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

static uint32_t fnv1a(const char *key) {
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619u;
    }
    return hash;
}

/* ========================================================================
 * lc_array — Dynamic growable array
 * ======================================================================== */

#define ARRAY_INITIAL_CAPACITY 8

lc_array lc_array_create(size_t element_size) {
    return (lc_array){
        .data         = NULL,
        .element_size = element_size,
        .count        = 0,
        .capacity     = 0,
    };
}

void lc_array_destroy(lc_array *arr) {
    lc_heap_free(arr->data);
    arr->data     = NULL;
    arr->count    = 0;
    arr->capacity = 0;
}

lc_result lc_array_reserve(lc_array *arr, size_t min_capacity) {
    if (min_capacity <= arr->capacity) return lc_ok(0);

    size_t new_cap = arr->capacity ? arr->capacity : ARRAY_INITIAL_CAPACITY;
    while (new_cap < min_capacity) new_cap *= 2;

    size_t new_size = new_cap * arr->element_size;
    lc_result_ptr alloc = lc_heap_reallocate(arr->data, new_size);
    if (lc_ptr_is_err(alloc)) return lc_err(LC_ERR_NOMEM);

    arr->data     = alloc.value;
    arr->capacity = new_cap;
    return lc_ok(0);
}

lc_result_ptr lc_array_push(lc_array *arr, const void *element) {
    if (arr->count == arr->capacity) {
        lc_result r = lc_array_reserve(arr, arr->count + 1);
        if (lc_is_err(r)) return lc_err_ptr(LC_ERR_NOMEM);
    }
    uint8_t *dst = arr->data + arr->count * arr->element_size;
    lc_bytes_copy(dst, element, arr->element_size);
    arr->count++;
    return lc_ok_ptr(dst);
}

bool lc_array_pop(lc_array *arr, void *out) {
    if (arr->count == 0) return false;
    arr->count--;
    uint8_t *src = arr->data + arr->count * arr->element_size;
    lc_bytes_copy(out, src, arr->element_size);
    return true;
}

void *lc_array_get(const lc_array *arr, size_t index) {
    if (index >= arr->count) return NULL;
    return arr->data + index * arr->element_size;
}

size_t lc_array_count(const lc_array *arr) {
    return arr->count;
}

bool lc_array_is_empty(const lc_array *arr) {
    return arr->count == 0;
}

void lc_array_clear(lc_array *arr) {
    arr->count = 0;
}

lc_result_ptr lc_array_insert(lc_array *arr, size_t index, const void *element) {
    if (index > arr->count) return lc_err_ptr(LC_ERR_INVAL);
    if (arr->count == arr->capacity) {
        lc_result r = lc_array_reserve(arr, arr->count + 1);
        if (lc_is_err(r)) return lc_err_ptr(LC_ERR_NOMEM);
    }
    uint8_t *slot = arr->data + index * arr->element_size;
    size_t tail_bytes = (arr->count - index) * arr->element_size;
    if (tail_bytes > 0) {
        lc_bytes_move(slot + arr->element_size, slot, tail_bytes);
    }
    lc_bytes_copy(slot, element, arr->element_size);
    arr->count++;
    return lc_ok_ptr(slot);
}

void lc_array_remove(lc_array *arr, size_t index) {
    if (index >= arr->count) return;
    uint8_t *slot = arr->data + index * arr->element_size;
    size_t tail_bytes = (arr->count - index - 1) * arr->element_size;
    if (tail_bytes > 0) {
        lc_bytes_move(slot, slot + arr->element_size, tail_bytes);
    }
    arr->count--;
}

void *lc_array_data(const lc_array *arr) {
    return arr->data;
}

/* ========================================================================
 * lc_hashmap — Hash map with Robin Hood open addressing
 * ======================================================================== */

#define HASHMAP_INITIAL_CAPACITY 16
#define HASHMAP_LOAD_PERCENT     75

static char *hashmap_copy_key(const char *key) {
    size_t len = lc_string_length(key);
    lc_result_ptr alloc = lc_heap_allocate(len + 1);
    if (lc_ptr_is_err(alloc)) return NULL;
    char *copy = alloc.value;
    lc_bytes_copy(copy, key, len + 1);
    return copy;
}

static bool hashmap_grow(lc_hashmap *map);

lc_hashmap lc_hashmap_create(void) {
    lc_hashmap map = {
        .entries  = NULL,
        .capacity = 0,
        .count    = 0,
    };
    return map;
}

void lc_hashmap_destroy(lc_hashmap *map) {
    if (map->entries) {
        for (size_t i = 0; i < map->capacity; i++) {
            lc_heap_free(map->entries[i].key);
        }
        lc_heap_free(map->entries);
    }
    map->entries  = NULL;
    map->capacity = 0;
    map->count    = 0;
}

static bool hashmap_insert_entry(lc_hashmap_entry *entries, size_t capacity,
                                 char *key, void *value, uint32_t hash) {
    uint32_t idx      = hash & (uint32_t)(capacity - 1);
    uint32_t distance = 0;

    lc_hashmap_entry incoming = {
        .key      = key,
        .value    = value,
        .hash     = hash,
        .distance = 0,
    };

    while (true) {
        if (entries[idx].key == NULL) {
            incoming.distance = distance;
            entries[idx] = incoming;
            return true;
        }

        /* Key already exists — update value */
        if (entries[idx].hash == hash) {
            size_t key_len = lc_string_length(key);
            size_t entry_len = lc_string_length(entries[idx].key);
            if (lc_string_equal(entries[idx].key, entry_len, key, key_len)) {
                entries[idx].value = value;
                /* Free the incoming key copy since we keep the existing one */
                lc_heap_free(key);
                return false; /* false = replaced existing, count stays same */
            }
        }

        /* Robin Hood: steal from the rich */
        if (distance > entries[idx].distance) {
            incoming.distance = distance;
            lc_hashmap_entry tmp = entries[idx];
            entries[idx] = incoming;
            incoming = tmp;
            distance = incoming.distance;
        }

        distance++;
        idx = (idx + 1) & (uint32_t)(capacity - 1);
    }
}

static bool hashmap_grow(lc_hashmap *map) {
    size_t new_cap = map->capacity ? map->capacity * 2 : HASHMAP_INITIAL_CAPACITY;
    lc_result_ptr alloc = lc_heap_allocate_zeroed(new_cap * sizeof(lc_hashmap_entry));
    if (lc_ptr_is_err(alloc)) return false;
    lc_hashmap_entry *new_entries = alloc.value;

    /* Re-insert all existing entries */
    if (map->entries) {
        for (size_t i = 0; i < map->capacity; i++) {
            if (map->entries[i].key != NULL) {
                hashmap_insert_entry(new_entries, new_cap,
                                     map->entries[i].key,
                                     map->entries[i].value,
                                     map->entries[i].hash);
            }
        }
        lc_heap_free(map->entries);
    }

    map->entries  = new_entries;
    map->capacity = new_cap;
    return true;
}

lc_result lc_hashmap_set(lc_hashmap *map, const char *key, void *value) {
    /* Grow if needed: check load factor or uninitialized */
    if (map->capacity == 0 || (map->count + 1) * 100 > map->capacity * HASHMAP_LOAD_PERCENT) {
        if (!hashmap_grow(map)) return lc_err(LC_ERR_NOMEM);
    }

    uint32_t hash = fnv1a(key);
    char *key_copy = hashmap_copy_key(key);
    if (!key_copy) return lc_err(LC_ERR_NOMEM);

    bool inserted = hashmap_insert_entry(map->entries, map->capacity, key_copy, value, hash);
    if (inserted) {
        map->count++;
    }
    return lc_ok(0);
}

void *lc_hashmap_get(const lc_hashmap *map, const char *key) {
    if (map->capacity == 0) return NULL;

    uint32_t hash     = fnv1a(key);
    uint32_t idx      = hash & (uint32_t)(map->capacity - 1);
    uint32_t distance = 0;
    size_t key_len    = lc_string_length(key);

    while (true) {
        if (map->entries[idx].key == NULL) return NULL;
        if (distance > map->entries[idx].distance) return NULL;

        if (map->entries[idx].hash == hash) {
            size_t entry_len = lc_string_length(map->entries[idx].key);
            if (lc_string_equal(map->entries[idx].key, entry_len, key, key_len)) {
                return map->entries[idx].value;
            }
        }

        distance++;
        idx = (idx + 1) & (uint32_t)(map->capacity - 1);
    }
}

void *lc_hashmap_remove(lc_hashmap *map, const char *key) {
    if (map->capacity == 0) return NULL;

    uint32_t hash     = fnv1a(key);
    uint32_t idx      = hash & (uint32_t)(map->capacity - 1);
    uint32_t distance = 0;
    size_t key_len    = lc_string_length(key);

    while (true) {
        if (map->entries[idx].key == NULL) return NULL;
        if (distance > map->entries[idx].distance) return NULL;

        if (map->entries[idx].hash == hash) {
            size_t entry_len = lc_string_length(map->entries[idx].key);
            if (lc_string_equal(map->entries[idx].key, entry_len, key, key_len)) {
                void *removed_value = map->entries[idx].value;
                lc_heap_free(map->entries[idx].key);

                /* Backward-shift deletion: move subsequent entries back */
                uint32_t empty = idx;
                uint32_t next  = (idx + 1) & (uint32_t)(map->capacity - 1);
                while (map->entries[next].key != NULL && map->entries[next].distance > 0) {
                    map->entries[empty] = map->entries[next];
                    map->entries[empty].distance--;
                    empty = next;
                    next = (next + 1) & (uint32_t)(map->capacity - 1);
                }
                map->entries[empty].key      = NULL;
                map->entries[empty].value    = NULL;
                map->entries[empty].hash     = 0;
                map->entries[empty].distance = 0;

                map->count--;
                return removed_value;
            }
        }

        distance++;
        idx = (idx + 1) & (uint32_t)(map->capacity - 1);
    }
}

bool lc_hashmap_contains(const lc_hashmap *map, const char *key) {
    if (map->capacity == 0) return false;

    uint32_t hash     = fnv1a(key);
    uint32_t idx      = hash & (uint32_t)(map->capacity - 1);
    uint32_t distance = 0;
    size_t key_len    = lc_string_length(key);

    while (true) {
        if (map->entries[idx].key == NULL) return false;
        if (distance > map->entries[idx].distance) return false;

        if (map->entries[idx].hash == hash) {
            size_t entry_len = lc_string_length(map->entries[idx].key);
            if (lc_string_equal(map->entries[idx].key, entry_len, key, key_len)) {
                return true;
            }
        }

        distance++;
        idx = (idx + 1) & (uint32_t)(map->capacity - 1);
    }
}

size_t lc_hashmap_count(const lc_hashmap *map) {
    return map->count;
}

void lc_hashmap_iterate(const lc_hashmap *map,
                        bool (*func)(const char *key, void *value, void *user_data),
                        void *user_data) {
    for (size_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key != NULL) {
            if (!func(map->entries[i].key, map->entries[i].value, user_data)) {
                return;
            }
        }
    }
}

/* ========================================================================
 * lc_ringbuf — Fixed-size ring buffer
 * ======================================================================== */

lc_ringbuf lc_ringbuf_create(size_t element_size, size_t min_capacity) {
    size_t capacity = next_power_of_2(min_capacity);
    lc_result_ptr alloc = lc_heap_allocate(capacity * element_size);

    if (lc_ptr_is_err(alloc)) {
        return (lc_ringbuf){
            .data         = NULL,
            .element_size = element_size,
            .capacity     = 0,
            .mask         = 0,
            .head         = 0,
            .tail         = 0,
        };
    }

    return (lc_ringbuf){
        .data         = alloc.value,
        .element_size = element_size,
        .capacity     = capacity,
        .mask         = capacity - 1,
        .head         = 0,
        .tail         = 0,
    };
}

void lc_ringbuf_destroy(lc_ringbuf *ring) {
    lc_heap_free(ring->data);
    ring->data     = NULL;
    ring->capacity = 0;
    ring->mask     = 0;
    ring->head     = 0;
    ring->tail     = 0;
}

lc_result lc_ringbuf_push(lc_ringbuf *ring, const void *element) {
    if (ring->data == NULL) return lc_err(LC_ERR_FULL);
    if (lc_ringbuf_is_full(ring)) return lc_err(LC_ERR_FULL);

    size_t offset = (ring->tail & ring->mask) * ring->element_size;
    lc_bytes_copy(ring->data + offset, element, ring->element_size);
    ring->tail++;
    return lc_ok(0);
}

bool lc_ringbuf_pop(lc_ringbuf *ring, void *out) {
    if (lc_ringbuf_is_empty(ring)) return false;

    size_t offset = (ring->head & ring->mask) * ring->element_size;
    lc_bytes_copy(out, ring->data + offset, ring->element_size);
    ring->head++;
    return true;
}

void *lc_ringbuf_peek(const lc_ringbuf *ring) {
    if (lc_ringbuf_is_empty(ring)) return NULL;

    size_t offset = (ring->head & ring->mask) * ring->element_size;
    return ring->data + offset;
}

size_t lc_ringbuf_count(const lc_ringbuf *ring) {
    return ring->tail - ring->head;
}

bool lc_ringbuf_is_empty(const lc_ringbuf *ring) {
    return ring->head == ring->tail;
}

bool lc_ringbuf_is_full(const lc_ringbuf *ring) {
    return lc_ringbuf_count(ring) == ring->capacity;
}

void lc_ringbuf_clear(lc_ringbuf *ring) {
    ring->head = 0;
    ring->tail = 0;
}

/* ========================================================================
 * lc_list — Intrusive doubly-linked list
 * ======================================================================== */

void lc_list_init(lc_list *list) {
    list->head.prev = &list->head;
    list->head.next = &list->head;
    list->count     = 0;
}

static void list_insert_between(lc_list_node *node,
                                lc_list_node *prev,
                                lc_list_node *next) {
    node->prev = prev;
    node->next = next;
    prev->next = node;
    next->prev = node;
}

void lc_list_push_front(lc_list *list, lc_list_node *node) {
    list_insert_between(node, &list->head, list->head.next);
    list->count++;
}

void lc_list_push_back(lc_list *list, lc_list_node *node) {
    list_insert_between(node, list->head.prev, &list->head);
    list->count++;
}

void lc_list_remove(lc_list *list, lc_list_node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = NULL;
    node->next = NULL;
    list->count--;
}

lc_list_node *lc_list_pop_front(lc_list *list) {
    if (lc_list_is_empty(list)) return NULL;
    lc_list_node *node = list->head.next;
    lc_list_remove(list, node);
    return node;
}

lc_list_node *lc_list_pop_back(lc_list *list) {
    if (lc_list_is_empty(list)) return NULL;
    lc_list_node *node = list->head.prev;
    lc_list_remove(list, node);
    return node;
}

lc_list_node *lc_list_front(const lc_list *list) {
    if (lc_list_is_empty(list)) return NULL;
    return list->head.next;
}

lc_list_node *lc_list_back(const lc_list *list) {
    if (lc_list_is_empty(list)) return NULL;
    return list->head.prev;
}

bool lc_list_is_empty(const lc_list *list) {
    return list->count == 0;
}

size_t lc_list_count(const lc_list *list) {
    return list->count;
}
