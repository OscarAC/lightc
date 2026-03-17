/*
 * bench_containers.c — data structure benchmarks.
 *
 * Measures: array push/pop, hashmap set/get, ringbuf push/pop, list ops.
 */

#include "bench.h"
#include <lightc/heap.h>
#include <lightdata/array.h>
#include <lightdata/hashmap.h>
#include <lightdata/ringbuf.h>
#include <lightdata/list.h>

/* --- Array --- */

static void bench_array_push(bench_state *b) {
    lc_array arr = lc_array_create(sizeof(int64_t));
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_array_push(&arr, &i);
    }
    lc_array_destroy(&arr);
}

static void bench_array_push_pop(bench_state *b) {
    lc_array arr = lc_array_create(sizeof(int64_t));
    int64_t val;
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_array_push(&arr, &i);
        lc_array_pop(&arr, &val);
    }
    lc_array_destroy(&arr);
}

static void bench_array_get(bench_state *b) {
    lc_array arr = lc_array_create(sizeof(int64_t));
    /* Pre-fill */
    for (int64_t i = 0; i < 10000; i++)
        lc_array_push(&arr, &i);
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile void *p = lc_array_get(&arr, (size_t)(i % 10000));
        (void)p;
    }
    lc_array_destroy(&arr);
}

/* --- HashMap --- */

/* Pre-generate key strings for hashmap benchmarks */
#define HMAP_KEYS 10000
static char hmap_keys[HMAP_KEYS][16];
static bool hmap_keys_init = false;

static void ensure_hmap_keys(void) {
    if (hmap_keys_init) return;
    for (int i = 0; i < HMAP_KEYS; i++) {
        /* Simple int-to-string: "key_NNNNN" */
        char *k = hmap_keys[i];
        k[0] = 'k'; k[1] = 'e'; k[2] = 'y'; k[3] = '_';
        int n = i;
        int pos = 4;
        /* Write digits in reverse, then reverse them */
        int start = pos;
        do {
            k[pos++] = '0' + (n % 10);
            n /= 10;
        } while (n > 0);
        k[pos] = '\0';
        /* Reverse the digit portion */
        for (int a = start, z = pos - 1; a < z; a++, z--) {
            char tmp = k[a]; k[a] = k[z]; k[z] = tmp;
        }
    }
    hmap_keys_init = true;
}

static void bench_hashmap_set(bench_state *b) {
    ensure_hmap_keys();
    lc_hashmap map = lc_hashmap_create();
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_hashmap_set(&map, hmap_keys[i % HMAP_KEYS], (void *)i);
    }
    lc_hashmap_destroy(&map);
}

static void bench_hashmap_get(bench_state *b) {
    ensure_hmap_keys();
    lc_hashmap map = lc_hashmap_create();
    /* Pre-fill */
    for (int i = 0; i < HMAP_KEYS; i++)
        lc_hashmap_set(&map, hmap_keys[i], (void *)(int64_t)i);
    for (int64_t i = 0; i < b->iterations; i++) {
        volatile void *v = lc_hashmap_get(&map, hmap_keys[i % HMAP_KEYS]);
        (void)v;
    }
    lc_hashmap_destroy(&map);
}

static void bench_hashmap_set_remove(bench_state *b) {
    ensure_hmap_keys();
    lc_hashmap map = lc_hashmap_create();
    for (int64_t i = 0; i < b->iterations; i++) {
        int idx = (int)(i % HMAP_KEYS);
        lc_hashmap_set(&map, hmap_keys[idx], (void *)i);
        lc_hashmap_remove(&map, hmap_keys[idx]);
    }
    lc_hashmap_destroy(&map);
}

/* --- RingBuffer --- */

static void bench_ringbuf_push_pop(bench_state *b) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int64_t), 1024);
    int64_t val;
    for (int64_t i = 0; i < b->iterations; i++) {
        lc_ringbuf_push(&ring, &i);
        lc_ringbuf_pop(&ring, &val);
    }
    lc_ringbuf_destroy(&ring);
}

static void bench_ringbuf_fill_drain(bench_state *b) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int64_t), 1024);
    int64_t val;
    for (int64_t i = 0; i < b->iterations; i++) {
        /* Fill to capacity */
        for (int j = 0; j < 1024; j++)
            lc_ringbuf_push(&ring, &val);
        /* Drain all */
        for (int j = 0; j < 1024; j++)
            lc_ringbuf_pop(&ring, &val);
    }
    lc_ringbuf_destroy(&ring);
}

/* --- Linked List --- */

typedef struct {
    lc_list_node node;
    int64_t value;
} list_item;

static void bench_list_push_pop(bench_state *b) {
    lc_list head;
    lc_list_init(&head);
    list_item items[64];
    for (int64_t i = 0; i < b->iterations; i++) {
        items[0].value = i;
        lc_list_push_back(&head, &items[0].node);
        lc_list_pop_front(&head);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    BENCH_SUITE("array");
    BENCH_RUN("push",      bench_array_push);
    BENCH_RUN("push+pop",  bench_array_push_pop);
    BENCH_RUN("get (10K)",  bench_array_get);

    BENCH_SUITE("hashmap");
    BENCH_RUN("set",         bench_hashmap_set);
    BENCH_RUN("get (10K)",   bench_hashmap_get);
    BENCH_RUN("set+remove",  bench_hashmap_set_remove);

    BENCH_SUITE("ringbuffer");
    BENCH_RUN("push+pop",         bench_ringbuf_push_pop);
    BENCH_RUN("fill+drain 1024",   bench_ringbuf_fill_drain);

    BENCH_SUITE("linked list");
    BENCH_RUN("push_back+pop_front", bench_list_push_pop);

    return 0;
}
