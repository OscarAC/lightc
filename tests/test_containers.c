/*
 * test_containers.c — tests for lightc containers (array, hashmap, ringbuf, list).
 */

#include "test.h"
#include <lightc/string.h>
#include <lightc/format.h>
#include <lightdata/array.h>
#include <lightdata/hashmap.h>
#include <lightdata/ringbuf.h>
#include <lightdata/list.h>

/* ========================================================================
 * lc_array tests
 * ======================================================================== */

static void test_array_create(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    TEST_ASSERT_EQ(lc_array_count(&arr), 0);
    TEST_ASSERT(lc_array_is_empty(&arr));
    lc_array_destroy(&arr);
}

static void test_array_push_and_get(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t val = 42;
    void *ptr = lc_array_push(&arr, &val);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQ(lc_array_count(&arr), 1);
    TEST_ASSERT(!lc_array_is_empty(&arr));

    int32_t *got = lc_array_get(&arr, 0);
    TEST_ASSERT_EQ(*got, 42);
    lc_array_destroy(&arr);
}

static void test_array_push_multiple(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    for (int32_t i = 0; i < 100; i++) {
        lc_array_push(&arr, &i);
    }
    TEST_ASSERT_EQ(lc_array_count(&arr), 100);

    for (int32_t i = 0; i < 100; i++) {
        int32_t *got = lc_array_get(&arr, (size_t)i);
        TEST_ASSERT_EQ(*got, i);
    }
    lc_array_destroy(&arr);
}

static void test_array_pop(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t val = 99;
    lc_array_push(&arr, &val);

    int32_t out = 0;
    bool ok = lc_array_pop(&arr, &out);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(out, 99);
    TEST_ASSERT_EQ(lc_array_count(&arr), 0);
    lc_array_destroy(&arr);
}

static void test_array_pop_empty(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t out = 0;
    bool ok = lc_array_pop(&arr, &out);
    TEST_ASSERT(!ok);
    lc_array_destroy(&arr);
}

static void test_array_clear(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t val = 1;
    lc_array_push(&arr, &val);
    lc_array_push(&arr, &val);
    lc_array_push(&arr, &val);
    TEST_ASSERT_EQ(lc_array_count(&arr), 3);

    lc_array_clear(&arr);
    TEST_ASSERT_EQ(lc_array_count(&arr), 0);
    TEST_ASSERT(lc_array_is_empty(&arr));
    lc_array_destroy(&arr);
}

static void test_array_insert(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t vals[] = {10, 20, 30};
    lc_array_push(&arr, &vals[0]);
    lc_array_push(&arr, &vals[2]);

    /* Insert 20 at index 1 */
    void *ptr = lc_array_insert(&arr, 1, &vals[1]);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQ(lc_array_count(&arr), 3);

    TEST_ASSERT_EQ(*(int32_t *)lc_array_get(&arr, 0), 10);
    TEST_ASSERT_EQ(*(int32_t *)lc_array_get(&arr, 1), 20);
    TEST_ASSERT_EQ(*(int32_t *)lc_array_get(&arr, 2), 30);
    lc_array_destroy(&arr);
}

static void test_array_remove(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t vals[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) lc_array_push(&arr, &vals[i]);

    lc_array_remove(&arr, 1); /* remove 20 */
    TEST_ASSERT_EQ(lc_array_count(&arr), 2);
    TEST_ASSERT_EQ(*(int32_t *)lc_array_get(&arr, 0), 10);
    TEST_ASSERT_EQ(*(int32_t *)lc_array_get(&arr, 1), 30);
    lc_array_destroy(&arr);
}

static void test_array_reserve(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    bool ok = lc_array_reserve(&arr, 1024);
    TEST_ASSERT(ok);
    /* Should be able to push without reallocation */
    for (int32_t i = 0; i < 1024; i++) {
        lc_array_push(&arr, &i);
    }
    TEST_ASSERT_EQ(lc_array_count(&arr), 1024);
    lc_array_destroy(&arr);
}

static void test_array_destroy(void) {
    lc_array arr = lc_array_create(sizeof(int32_t));
    int32_t val = 1;
    lc_array_push(&arr, &val);
    lc_array_destroy(&arr);
    TEST_ASSERT_EQ(lc_array_count(&arr), 0);
    TEST_ASSERT_NULL(lc_array_data(&arr));
}

/* ========================================================================
 * lc_hashmap tests
 * ======================================================================== */

static void test_hashmap_create(void) {
    lc_hashmap map = lc_hashmap_create();
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 0);
    lc_hashmap_destroy(&map);
}

static void test_hashmap_set_and_get(void) {
    lc_hashmap map = lc_hashmap_create();
    bool ok = lc_hashmap_set(&map, "key1", (void *)100);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 1);

    void *val = lc_hashmap_get(&map, "key1");
    TEST_ASSERT_EQ((uintptr_t)val, 100);
    lc_hashmap_destroy(&map);
}

static void test_hashmap_get_missing(void) {
    lc_hashmap map = lc_hashmap_create();
    void *val = lc_hashmap_get(&map, "nonexistent");
    TEST_ASSERT_NULL(val);
    lc_hashmap_destroy(&map);
}

static void test_hashmap_overwrite(void) {
    lc_hashmap map = lc_hashmap_create();
    lc_hashmap_set(&map, "key", (void *)1);
    lc_hashmap_set(&map, "key", (void *)2);
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 1);
    TEST_ASSERT_EQ((uintptr_t)lc_hashmap_get(&map, "key"), 2);
    lc_hashmap_destroy(&map);
}

static void test_hashmap_remove(void) {
    lc_hashmap map = lc_hashmap_create();
    lc_hashmap_set(&map, "key", (void *)42);
    void *removed = lc_hashmap_remove(&map, "key");
    TEST_ASSERT_EQ((uintptr_t)removed, 42);
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 0);
    TEST_ASSERT_NULL(lc_hashmap_get(&map, "key"));
    lc_hashmap_destroy(&map);
}

static void test_hashmap_remove_missing(void) {
    lc_hashmap map = lc_hashmap_create();
    void *removed = lc_hashmap_remove(&map, "nope");
    TEST_ASSERT_NULL(removed);
    lc_hashmap_destroy(&map);
}

static void test_hashmap_contains(void) {
    lc_hashmap map = lc_hashmap_create();
    lc_hashmap_set(&map, "present", (void *)1);
    TEST_ASSERT(lc_hashmap_contains(&map, "present"));
    TEST_ASSERT(!lc_hashmap_contains(&map, "absent"));
    lc_hashmap_destroy(&map);
}

static void test_hashmap_count(void) {
    lc_hashmap map = lc_hashmap_create();
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 0);
    lc_hashmap_set(&map, "a", (void *)1);
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 1);
    lc_hashmap_set(&map, "b", (void *)2);
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 2);
    lc_hashmap_remove(&map, "a");
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 1);
    lc_hashmap_destroy(&map);
}

/* Callback for iterate test */
static bool _iterate_sum_cb(const char *key, void *value, void *user_data) {
    (void)key;
    uintptr_t *sum = user_data;
    *sum += (uintptr_t)value;
    return true;
}

static void test_hashmap_iterate(void) {
    lc_hashmap map = lc_hashmap_create();
    lc_hashmap_set(&map, "a", (void *)10);
    lc_hashmap_set(&map, "b", (void *)20);
    lc_hashmap_set(&map, "c", (void *)30);

    uintptr_t sum = 0;
    lc_hashmap_iterate(&map, _iterate_sum_cb, &sum);
    TEST_ASSERT_EQ(sum, 60);
    lc_hashmap_destroy(&map);
}

static void test_hashmap_many_keys(void) {
    /*
     * Insert many keys to force hash collisions and table growth.
     * We generate keys like "key_000" .. "key_199" using the format builder.
     */
    lc_hashmap map = lc_hashmap_create();
    char keybuf[16];

    for (uintptr_t i = 0; i < 200; i++) {
        lc_format fmt = lc_format_start(keybuf, sizeof(keybuf));
        lc_format_add_text(&fmt, "key_");
        lc_format_add_unsigned_padded(&fmt, i, 3, '0');
        lc_format_finish(&fmt);

        bool ok = lc_hashmap_set(&map, keybuf, (void *)i);
        TEST_ASSERT(ok);
    }
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 200);

    /* Verify all keys are retrievable */
    for (uintptr_t i = 0; i < 200; i++) {
        lc_format fmt = lc_format_start(keybuf, sizeof(keybuf));
        lc_format_add_text(&fmt, "key_");
        lc_format_add_unsigned_padded(&fmt, i, 3, '0');
        lc_format_finish(&fmt);

        void *val = lc_hashmap_get(&map, keybuf);
        TEST_ASSERT_EQ((uintptr_t)val, i);
    }

    lc_hashmap_destroy(&map);
}

static void test_hashmap_destroy(void) {
    lc_hashmap map = lc_hashmap_create();
    lc_hashmap_set(&map, "x", (void *)1);
    lc_hashmap_set(&map, "y", (void *)2);
    lc_hashmap_destroy(&map);
    TEST_ASSERT_EQ(lc_hashmap_count(&map), 0);
}

/* ========================================================================
 * lc_ringbuf tests
 * ======================================================================== */

static void test_ringbuf_create(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    TEST_ASSERT(lc_ringbuf_is_empty(&ring));
    TEST_ASSERT(!lc_ringbuf_is_full(&ring));
    TEST_ASSERT_EQ(lc_ringbuf_count(&ring), 0);
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_push_pop(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    int32_t val = 42;
    bool ok = lc_ringbuf_push(&ring, &val);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(lc_ringbuf_count(&ring), 1);

    int32_t out = 0;
    ok = lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(out, 42);
    TEST_ASSERT_EQ(lc_ringbuf_count(&ring), 0);
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_peek(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    int32_t val = 77;
    lc_ringbuf_push(&ring, &val);

    int32_t *peeked = lc_ringbuf_peek(&ring);
    TEST_ASSERT_NOT_NULL(peeked);
    TEST_ASSERT_EQ(*peeked, 77);
    /* Count should not change */
    TEST_ASSERT_EQ(lc_ringbuf_count(&ring), 1);
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_peek_empty(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    TEST_ASSERT_NULL(lc_ringbuf_peek(&ring));
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_pop_empty(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    int32_t out = 0;
    bool ok = lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT(!ok);
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_full(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 4);
    /* Capacity is next power of 2, so 4 */
    for (int32_t i = 0; i < 4; i++) {
        bool ok = lc_ringbuf_push(&ring, &i);
        TEST_ASSERT(ok);
    }
    TEST_ASSERT(lc_ringbuf_is_full(&ring));

    /* Push should fail when full */
    int32_t extra = 99;
    bool ok = lc_ringbuf_push(&ring, &extra);
    TEST_ASSERT(!ok);
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_wraparound(void) {
    /*
     * Fill, drain half, fill again to exercise wrap-around.
     * Capacity = 4 (power of 2).
     */
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 4);

    /* Push 4 elements: 0, 1, 2, 3 */
    for (int32_t i = 0; i < 4; i++) {
        lc_ringbuf_push(&ring, &i);
    }

    /* Pop 2 elements: should get 0, 1 */
    int32_t out;
    lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT_EQ(out, 0);
    lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT_EQ(out, 1);

    /* Now count=2, push 2 more: 10, 11 (these wrap around) */
    int32_t v10 = 10, v11 = 11;
    bool ok1 = lc_ringbuf_push(&ring, &v10);
    bool ok2 = lc_ringbuf_push(&ring, &v11);
    TEST_ASSERT(ok1);
    TEST_ASSERT(ok2);
    TEST_ASSERT_EQ(lc_ringbuf_count(&ring), 4);

    /* Pop all: should get 2, 3, 10, 11 */
    lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT_EQ(out, 2);
    lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT_EQ(out, 3);
    lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT_EQ(out, 10);
    lc_ringbuf_pop(&ring, &out);
    TEST_ASSERT_EQ(out, 11);

    TEST_ASSERT(lc_ringbuf_is_empty(&ring));
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_clear(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    int32_t val = 1;
    lc_ringbuf_push(&ring, &val);
    lc_ringbuf_push(&ring, &val);
    lc_ringbuf_clear(&ring);
    TEST_ASSERT(lc_ringbuf_is_empty(&ring));
    TEST_ASSERT_EQ(lc_ringbuf_count(&ring), 0);
    lc_ringbuf_destroy(&ring);
}

static void test_ringbuf_destroy(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    int32_t val = 5;
    lc_ringbuf_push(&ring, &val);
    lc_ringbuf_destroy(&ring);
    TEST_ASSERT_NULL(ring.data);
}

static void test_ringbuf_fifo_order(void) {
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 16);
    for (int32_t i = 0; i < 10; i++) {
        lc_ringbuf_push(&ring, &i);
    }
    for (int32_t i = 0; i < 10; i++) {
        int32_t out;
        lc_ringbuf_pop(&ring, &out);
        TEST_ASSERT_EQ(out, i);
    }
    lc_ringbuf_destroy(&ring);
}

/* ========================================================================
 * lc_list tests
 * ======================================================================== */

typedef struct {
    int value;
    lc_list_node node;
} test_item;

static void test_list_init(void) {
    lc_list list;
    lc_list_init(&list);
    TEST_ASSERT(lc_list_is_empty(&list));
    TEST_ASSERT_EQ(lc_list_count(&list), 0);
    TEST_ASSERT_NULL(lc_list_front(&list));
    TEST_ASSERT_NULL(lc_list_back(&list));
}

static void test_list_push_front(void) {
    lc_list list;
    lc_list_init(&list);

    test_item a = { .value = 10 };
    test_item b = { .value = 20 };

    lc_list_push_front(&list, &a.node);
    lc_list_push_front(&list, &b.node);

    TEST_ASSERT_EQ(lc_list_count(&list), 2);
    TEST_ASSERT(!lc_list_is_empty(&list));

    /* Front should be b (pushed last to front) */
    lc_list_node *front = lc_list_front(&list);
    test_item *front_item = lc_list_entry(front, test_item, node);
    TEST_ASSERT_EQ(front_item->value, 20);

    /* Back should be a */
    lc_list_node *back = lc_list_back(&list);
    test_item *back_item = lc_list_entry(back, test_item, node);
    TEST_ASSERT_EQ(back_item->value, 10);
}

static void test_list_push_back(void) {
    lc_list list;
    lc_list_init(&list);

    test_item a = { .value = 10 };
    test_item b = { .value = 20 };

    lc_list_push_back(&list, &a.node);
    lc_list_push_back(&list, &b.node);

    TEST_ASSERT_EQ(lc_list_count(&list), 2);

    /* Front should be a (pushed first) */
    lc_list_node *front = lc_list_front(&list);
    test_item *front_item = lc_list_entry(front, test_item, node);
    TEST_ASSERT_EQ(front_item->value, 10);

    /* Back should be b */
    lc_list_node *back = lc_list_back(&list);
    test_item *back_item = lc_list_entry(back, test_item, node);
    TEST_ASSERT_EQ(back_item->value, 20);
}

static void test_list_pop_front(void) {
    lc_list list;
    lc_list_init(&list);

    test_item a = { .value = 10 };
    test_item b = { .value = 20 };

    lc_list_push_back(&list, &a.node);
    lc_list_push_back(&list, &b.node);

    lc_list_node *popped = lc_list_pop_front(&list);
    test_item *item = lc_list_entry(popped, test_item, node);
    TEST_ASSERT_EQ(item->value, 10);
    TEST_ASSERT_EQ(lc_list_count(&list), 1);
}

static void test_list_pop_back(void) {
    lc_list list;
    lc_list_init(&list);

    test_item a = { .value = 10 };
    test_item b = { .value = 20 };

    lc_list_push_back(&list, &a.node);
    lc_list_push_back(&list, &b.node);

    lc_list_node *popped = lc_list_pop_back(&list);
    test_item *item = lc_list_entry(popped, test_item, node);
    TEST_ASSERT_EQ(item->value, 20);
    TEST_ASSERT_EQ(lc_list_count(&list), 1);
}

static void test_list_pop_empty(void) {
    lc_list list;
    lc_list_init(&list);
    TEST_ASSERT_NULL(lc_list_pop_front(&list));
    TEST_ASSERT_NULL(lc_list_pop_back(&list));
}

static void test_list_remove(void) {
    lc_list list;
    lc_list_init(&list);

    test_item a = { .value = 10 };
    test_item b = { .value = 20 };
    test_item c = { .value = 30 };

    lc_list_push_back(&list, &a.node);
    lc_list_push_back(&list, &b.node);
    lc_list_push_back(&list, &c.node);

    /* Remove middle element */
    lc_list_remove(&list, &b.node);
    TEST_ASSERT_EQ(lc_list_count(&list), 2);

    lc_list_node *front = lc_list_front(&list);
    test_item *front_item = lc_list_entry(front, test_item, node);
    TEST_ASSERT_EQ(front_item->value, 10);

    lc_list_node *back = lc_list_back(&list);
    test_item *back_item = lc_list_entry(back, test_item, node);
    TEST_ASSERT_EQ(back_item->value, 30);
}

static void test_list_for_each(void) {
    lc_list list;
    lc_list_init(&list);

    test_item items[5];
    for (int i = 0; i < 5; i++) {
        items[i].value = (i + 1) * 10;
        lc_list_push_back(&list, &items[i].node);
    }

    /* Sum values by iterating */
    int sum = 0;
    lc_list_node *n;
    lc_list_for_each(n, &list) {
        test_item *it = lc_list_entry(n, test_item, node);
        sum += it->value;
    }
    /* 10 + 20 + 30 + 40 + 50 = 150 */
    TEST_ASSERT_EQ(sum, 150);
}

static void test_list_is_empty(void) {
    lc_list list;
    lc_list_init(&list);
    TEST_ASSERT(lc_list_is_empty(&list));

    test_item a = { .value = 1 };
    lc_list_push_back(&list, &a.node);
    TEST_ASSERT(!lc_list_is_empty(&list));

    lc_list_pop_front(&list);
    TEST_ASSERT(lc_list_is_empty(&list));
}

static void test_list_count(void) {
    lc_list list;
    lc_list_init(&list);
    TEST_ASSERT_EQ(lc_list_count(&list), 0);

    test_item a = { .value = 1 };
    test_item b = { .value = 2 };
    test_item c = { .value = 3 };

    lc_list_push_back(&list, &a.node);
    TEST_ASSERT_EQ(lc_list_count(&list), 1);
    lc_list_push_back(&list, &b.node);
    TEST_ASSERT_EQ(lc_list_count(&list), 2);
    lc_list_push_back(&list, &c.node);
    TEST_ASSERT_EQ(lc_list_count(&list), 3);

    lc_list_pop_front(&list);
    TEST_ASSERT_EQ(lc_list_count(&list), 2);
}

static void test_list_front_back(void) {
    lc_list list;
    lc_list_init(&list);

    test_item a = { .value = 100 };
    lc_list_push_back(&list, &a.node);

    /* Single element: front and back should be the same */
    lc_list_node *f = lc_list_front(&list);
    lc_list_node *b = lc_list_back(&list);
    TEST_ASSERT_EQ(f, b);

    test_item *fi = lc_list_entry(f, test_item, node);
    TEST_ASSERT_EQ(fi->value, 100);
}

/* ===== main ===== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Array tests */
    TEST_RUN(test_array_create);
    TEST_RUN(test_array_push_and_get);
    TEST_RUN(test_array_push_multiple);
    TEST_RUN(test_array_pop);
    TEST_RUN(test_array_pop_empty);
    TEST_RUN(test_array_clear);
    TEST_RUN(test_array_insert);
    TEST_RUN(test_array_remove);
    TEST_RUN(test_array_reserve);
    TEST_RUN(test_array_destroy);

    /* HashMap tests */
    TEST_RUN(test_hashmap_create);
    TEST_RUN(test_hashmap_set_and_get);
    TEST_RUN(test_hashmap_get_missing);
    TEST_RUN(test_hashmap_overwrite);
    TEST_RUN(test_hashmap_remove);
    TEST_RUN(test_hashmap_remove_missing);
    TEST_RUN(test_hashmap_contains);
    TEST_RUN(test_hashmap_count);
    TEST_RUN(test_hashmap_iterate);
    TEST_RUN(test_hashmap_many_keys);
    TEST_RUN(test_hashmap_destroy);

    /* RingBuffer tests */
    TEST_RUN(test_ringbuf_create);
    TEST_RUN(test_ringbuf_push_pop);
    TEST_RUN(test_ringbuf_peek);
    TEST_RUN(test_ringbuf_peek_empty);
    TEST_RUN(test_ringbuf_pop_empty);
    TEST_RUN(test_ringbuf_full);
    TEST_RUN(test_ringbuf_wraparound);
    TEST_RUN(test_ringbuf_clear);
    TEST_RUN(test_ringbuf_destroy);
    TEST_RUN(test_ringbuf_fifo_order);

    /* List tests */
    TEST_RUN(test_list_init);
    TEST_RUN(test_list_push_front);
    TEST_RUN(test_list_push_back);
    TEST_RUN(test_list_pop_front);
    TEST_RUN(test_list_pop_back);
    TEST_RUN(test_list_pop_empty);
    TEST_RUN(test_list_remove);
    TEST_RUN(test_list_for_each);
    TEST_RUN(test_list_is_empty);
    TEST_RUN(test_list_count);
    TEST_RUN(test_list_front_back);

    return test_main();
}
