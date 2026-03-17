/*
 * Test all containers: array, hashmap, ringbuf, list.
 */
#include <lightc/syscall.h>
#include <lightc/string.h>
#include <lightc/print.h>
#include <lightc/heap.h>
#include <lightdata/array.h>
#include <lightdata/hashmap.h>
#include <lightdata/ringbuf.h>
#include <lightdata/list.h>

#define S(literal) literal, sizeof(literal) - 1

static void say_pass_fail(bool passed) {
    if (passed) {
        lc_print_line(STDOUT, S(" PASS"));
    } else {
        lc_print_line(STDOUT, S(" FAIL"));
    }
}

/* ========================================================================
 * Array tests
 * ======================================================================== */

static void test_array(void) {
    lc_print_line(STDOUT, S("--- array ---"));

    /* create and empty check */
    lc_print_string(STDOUT, S("array_create_empty"));
    lc_array arr = lc_array_create(sizeof(int32_t));
    say_pass_fail(lc_array_count(&arr) == 0 && lc_array_is_empty(&arr));

    /* push and get */
    lc_print_string(STDOUT, S("array_push_get"));
    int32_t val = 42;
    void *p = lc_array_push(&arr, &val);
    int32_t *got = lc_array_get(&arr, 0);
    say_pass_fail(p != NULL && *got == 42 && lc_array_count(&arr) == 1);

    /* push multiple */
    lc_print_string(STDOUT, S("array_push_multiple"));
    bool ok = true;
    for (int32_t i = 1; i <= 20; i++) {
        if (!lc_array_push(&arr, &i)) { ok = false; break; }
    }
    say_pass_fail(ok && lc_array_count(&arr) == 21);

    /* get after growth */
    lc_print_string(STDOUT, S("array_get_after_growth"));
    int32_t *first = lc_array_get(&arr, 0);
    int32_t *last  = lc_array_get(&arr, 20);
    say_pass_fail(*first == 42 && *last == 20);

    /* pop */
    lc_print_string(STDOUT, S("array_pop"));
    int32_t popped;
    bool popped_ok = lc_array_pop(&arr, &popped);
    say_pass_fail(popped_ok && popped == 20 && lc_array_count(&arr) == 20);

    /* pop until empty */
    lc_print_string(STDOUT, S("array_pop_until_empty"));
    while (lc_array_pop(&arr, &popped)) {}
    say_pass_fail(lc_array_is_empty(&arr) && lc_array_count(&arr) == 0);

    /* pop from empty */
    lc_print_string(STDOUT, S("array_pop_empty"));
    say_pass_fail(!lc_array_pop(&arr, &popped));

    /* insert */
    lc_print_string(STDOUT, S("array_insert"));
    int32_t vals[] = {10, 20, 30};
    lc_array_push(&arr, &vals[0]);
    lc_array_push(&arr, &vals[2]);
    lc_array_insert(&arr, 1, &vals[1]); /* insert 20 between 10 and 30 */
    int32_t *a0 = lc_array_get(&arr, 0);
    int32_t *a1 = lc_array_get(&arr, 1);
    int32_t *a2 = lc_array_get(&arr, 2);
    say_pass_fail(*a0 == 10 && *a1 == 20 && *a2 == 30 && lc_array_count(&arr) == 3);

    /* remove */
    lc_print_string(STDOUT, S("array_remove"));
    lc_array_remove(&arr, 1); /* remove the 20 */
    a0 = lc_array_get(&arr, 0);
    a1 = lc_array_get(&arr, 1);
    say_pass_fail(*a0 == 10 && *a1 == 30 && lc_array_count(&arr) == 2);

    /* clear */
    lc_print_string(STDOUT, S("array_clear"));
    lc_array_clear(&arr);
    say_pass_fail(lc_array_is_empty(&arr) && lc_array_count(&arr) == 0);

    /* reserve */
    lc_print_string(STDOUT, S("array_reserve"));
    ok = lc_array_reserve(&arr, 1000);
    say_pass_fail(ok && arr.capacity >= 1000);

    /* data pointer */
    lc_print_string(STDOUT, S("array_data_pointer"));
    int32_t v1 = 99;
    lc_array_push(&arr, &v1);
    void *data = lc_array_data(&arr);
    say_pass_fail(data != NULL && *(int32_t *)data == 99);

    /* growth stress: push 500 elements */
    lc_print_string(STDOUT, S("array_growth_stress"));
    lc_array_clear(&arr);
    ok = true;
    for (int32_t i = 0; i < 500; i++) {
        if (!lc_array_push(&arr, &i)) { ok = false; break; }
    }
    /* verify integrity */
    for (int32_t i = 0; i < 500 && ok; i++) {
        if (*(int32_t *)lc_array_get(&arr, (size_t)i) != i) { ok = false; }
    }
    say_pass_fail(ok && lc_array_count(&arr) == 500);

    /* insert at front */
    lc_print_string(STDOUT, S("array_insert_front"));
    lc_array arr2 = lc_array_create(sizeof(int32_t));
    int32_t x = 3;
    lc_array_push(&arr2, &x);
    x = 1;
    lc_array_insert(&arr2, 0, &x);
    x = 2;
    lc_array_insert(&arr2, 1, &x);
    /* should be [1, 2, 3] */
    say_pass_fail(*(int32_t *)lc_array_get(&arr2, 0) == 1 &&
                  *(int32_t *)lc_array_get(&arr2, 1) == 2 &&
                  *(int32_t *)lc_array_get(&arr2, 2) == 3);
    lc_array_destroy(&arr2);

    /* remove from front */
    lc_print_string(STDOUT, S("array_remove_front"));
    lc_array arr3 = lc_array_create(sizeof(int32_t));
    for (int32_t i = 0; i < 5; i++) lc_array_push(&arr3, &i);
    lc_array_remove(&arr3, 0);
    say_pass_fail(*(int32_t *)lc_array_get(&arr3, 0) == 1 && lc_array_count(&arr3) == 4);
    lc_array_destroy(&arr3);

    lc_array_destroy(&arr);
}

/* ========================================================================
 * Hashmap tests
 * ======================================================================== */

static bool iterate_counter(const char *key, void *value, void *user_data) {
    (void)key; (void)value;
    size_t *counter = user_data;
    (*counter)++;
    return true;
}

static bool iterate_stop_at_3(const char *key, void *value, void *user_data) {
    (void)key; (void)value;
    size_t *counter = user_data;
    (*counter)++;
    return *counter < 3;
}

static void test_hashmap(void) {
    lc_print_line(STDOUT, S("--- hashmap ---"));

    /* create */
    lc_print_string(STDOUT, S("hashmap_create"));
    lc_hashmap map = lc_hashmap_create();
    say_pass_fail(lc_hashmap_count(&map) == 0);

    /* set and get */
    lc_print_string(STDOUT, S("hashmap_set_get"));
    int32_t v1 = 100;
    lc_hashmap_set(&map, "alpha", &v1);
    int32_t *got = lc_hashmap_get(&map, "alpha");
    say_pass_fail(got != NULL && *got == 100 && lc_hashmap_count(&map) == 1);

    /* overwrite */
    lc_print_string(STDOUT, S("hashmap_overwrite"));
    int32_t v2 = 200;
    lc_hashmap_set(&map, "alpha", &v2);
    got = lc_hashmap_get(&map, "alpha");
    say_pass_fail(got != NULL && *got == 200 && lc_hashmap_count(&map) == 1);

    /* multiple keys */
    lc_print_string(STDOUT, S("hashmap_multiple_keys"));
    int32_t v3 = 300, v4 = 400, v5 = 500;
    lc_hashmap_set(&map, "beta", &v3);
    lc_hashmap_set(&map, "gamma", &v4);
    lc_hashmap_set(&map, "delta", &v5);
    say_pass_fail(lc_hashmap_count(&map) == 4 &&
                  *(int32_t *)lc_hashmap_get(&map, "beta") == 300 &&
                  *(int32_t *)lc_hashmap_get(&map, "gamma") == 400 &&
                  *(int32_t *)lc_hashmap_get(&map, "delta") == 500);

    /* contains */
    lc_print_string(STDOUT, S("hashmap_contains"));
    say_pass_fail(lc_hashmap_contains(&map, "alpha") &&
                  lc_hashmap_contains(&map, "beta") &&
                  !lc_hashmap_contains(&map, "nonexistent"));

    /* get nonexistent */
    lc_print_string(STDOUT, S("hashmap_get_nonexistent"));
    say_pass_fail(lc_hashmap_get(&map, "nonexistent") == NULL);

    /* remove */
    lc_print_string(STDOUT, S("hashmap_remove"));
    void *removed = lc_hashmap_remove(&map, "beta");
    say_pass_fail(removed == &v3 && lc_hashmap_count(&map) == 3 &&
                  !lc_hashmap_contains(&map, "beta"));

    /* remove nonexistent */
    lc_print_string(STDOUT, S("hashmap_remove_nonexistent"));
    say_pass_fail(lc_hashmap_remove(&map, "nonexistent") == NULL);

    /* iterate count all */
    lc_print_string(STDOUT, S("hashmap_iterate"));
    size_t counter = 0;
    lc_hashmap_iterate(&map, iterate_counter, &counter);
    say_pass_fail(counter == 3);

    /* iterate with early stop */
    lc_print_string(STDOUT, S("hashmap_iterate_stop"));
    counter = 0;
    lc_hashmap_iterate(&map, iterate_stop_at_3, &counter);
    say_pass_fail(counter == 3);

    /* resize stress: insert many entries to trigger multiple growths */
    lc_print_string(STDOUT, S("hashmap_resize_stress"));
    lc_hashmap_destroy(&map);
    map = lc_hashmap_create();

    /* Build keys: "k000" through "k199" */
    char keys[200][8];
    int32_t values[200];
    bool ok = true;
    for (int32_t i = 0; i < 200; i++) {
        keys[i][0] = 'k';
        keys[i][1] = (char)('0' + (i / 100) % 10);
        keys[i][2] = (char)('0' + (i / 10) % 10);
        keys[i][3] = (char)('0' + i % 10);
        keys[i][4] = '\0';
        values[i] = i * 7;
        if (!lc_hashmap_set(&map, keys[i], &values[i])) { ok = false; break; }
    }
    say_pass_fail(ok && lc_hashmap_count(&map) == 200);

    /* verify all 200 entries */
    lc_print_string(STDOUT, S("hashmap_verify_all"));
    ok = true;
    for (int32_t i = 0; i < 200; i++) {
        int32_t *val = lc_hashmap_get(&map, keys[i]);
        if (val == NULL || *val != i * 7) { ok = false; break; }
    }
    say_pass_fail(ok);

    /* remove half and verify */
    lc_print_string(STDOUT, S("hashmap_remove_half"));
    ok = true;
    for (int32_t i = 0; i < 200; i += 2) {
        if (lc_hashmap_remove(&map, keys[i]) == NULL) { ok = false; break; }
    }
    say_pass_fail(ok && lc_hashmap_count(&map) == 100);

    /* verify remaining half */
    lc_print_string(STDOUT, S("hashmap_verify_remaining"));
    ok = true;
    for (int32_t i = 0; i < 200; i++) {
        int32_t *val = lc_hashmap_get(&map, keys[i]);
        if (i % 2 == 0) {
            if (val != NULL) { ok = false; break; }
        } else {
            if (val == NULL || *val != i * 7) { ok = false; break; }
        }
    }
    say_pass_fail(ok);

    /* string values (not just int pointers) */
    lc_print_string(STDOUT, S("hashmap_string_values"));
    lc_hashmap_destroy(&map);
    map = lc_hashmap_create();
    lc_hashmap_set(&map, "name", (void *)"lightc");
    lc_hashmap_set(&map, "lang", (void *)"c23");
    char *name = lc_hashmap_get(&map, "name");
    char *lang = lc_hashmap_get(&map, "lang");
    say_pass_fail(lc_string_equal(name, 6, "lightc", 6) &&
                  lc_string_equal(lang, 3, "c23", 3));

    lc_hashmap_destroy(&map);
}

/* ========================================================================
 * Ringbuf tests
 * ======================================================================== */

static void test_ringbuf(void) {
    lc_print_line(STDOUT, S("--- ringbuf ---"));

    /* create */
    lc_print_string(STDOUT, S("ringbuf_create"));
    lc_ringbuf ring = lc_ringbuf_create(sizeof(int32_t), 8);
    say_pass_fail(ring.data != NULL && ring.capacity == 8 && lc_ringbuf_is_empty(&ring));

    /* power-of-2 rounding */
    lc_print_string(STDOUT, S("ringbuf_power_of_2"));
    lc_ringbuf ring2 = lc_ringbuf_create(sizeof(int32_t), 5);
    lc_ringbuf ring3 = lc_ringbuf_create(sizeof(int32_t), 100);
    say_pass_fail(ring2.capacity == 8 && ring3.capacity == 128);
    lc_ringbuf_destroy(&ring2);
    lc_ringbuf_destroy(&ring3);

    /* push and pop */
    lc_print_string(STDOUT, S("ringbuf_push_pop"));
    int32_t val = 42;
    bool pushed = lc_ringbuf_push(&ring, &val);
    int32_t out;
    bool popped = lc_ringbuf_pop(&ring, &out);
    say_pass_fail(pushed && popped && out == 42);

    /* peek */
    lc_print_string(STDOUT, S("ringbuf_peek"));
    val = 99;
    lc_ringbuf_push(&ring, &val);
    int32_t *peeked = lc_ringbuf_peek(&ring);
    say_pass_fail(peeked != NULL && *peeked == 99 && lc_ringbuf_count(&ring) == 1);
    lc_ringbuf_pop(&ring, &out); /* clean up */

    /* peek empty */
    lc_print_string(STDOUT, S("ringbuf_peek_empty"));
    say_pass_fail(lc_ringbuf_peek(&ring) == NULL);

    /* fill to capacity */
    lc_print_string(STDOUT, S("ringbuf_fill_full"));
    bool ok = true;
    for (int32_t i = 0; i < 8; i++) {
        if (!lc_ringbuf_push(&ring, &i)) { ok = false; break; }
    }
    say_pass_fail(ok && lc_ringbuf_is_full(&ring) && lc_ringbuf_count(&ring) == 8);

    /* push when full */
    lc_print_string(STDOUT, S("ringbuf_push_full"));
    val = 999;
    say_pass_fail(!lc_ringbuf_push(&ring, &val));

    /* pop all */
    lc_print_string(STDOUT, S("ringbuf_pop_all"));
    ok = true;
    for (int32_t i = 0; i < 8; i++) {
        if (!lc_ringbuf_pop(&ring, &out) || out != i) { ok = false; break; }
    }
    say_pass_fail(ok && lc_ringbuf_is_empty(&ring));

    /* pop when empty */
    lc_print_string(STDOUT, S("ringbuf_pop_empty"));
    say_pass_fail(!lc_ringbuf_pop(&ring, &out));

    /* wrap-around: push capacity, pop half, push more */
    lc_print_string(STDOUT, S("ringbuf_wrap_around"));
    for (int32_t i = 0; i < 8; i++) lc_ringbuf_push(&ring, &i);
    /* Pop 4 */
    for (int32_t i = 0; i < 4; i++) lc_ringbuf_pop(&ring, &out);
    /* Push 4 more (these wrap around) */
    ok = true;
    for (int32_t i = 100; i < 104; i++) {
        if (!lc_ringbuf_push(&ring, &i)) { ok = false; break; }
    }
    say_pass_fail(ok && lc_ringbuf_count(&ring) == 8 && lc_ringbuf_is_full(&ring));

    /* Verify wrap-around data integrity */
    lc_print_string(STDOUT, S("ringbuf_wrap_integrity"));
    int32_t expected[] = {4, 5, 6, 7, 100, 101, 102, 103};
    ok = true;
    for (int i = 0; i < 8; i++) {
        if (!lc_ringbuf_pop(&ring, &out) || out != expected[i]) { ok = false; break; }
    }
    say_pass_fail(ok && lc_ringbuf_is_empty(&ring));

    /* clear */
    lc_print_string(STDOUT, S("ringbuf_clear"));
    for (int32_t i = 0; i < 5; i++) lc_ringbuf_push(&ring, &i);
    lc_ringbuf_clear(&ring);
    say_pass_fail(lc_ringbuf_is_empty(&ring) && lc_ringbuf_count(&ring) == 0);

    /* count */
    lc_print_string(STDOUT, S("ringbuf_count"));
    for (int32_t i = 0; i < 3; i++) lc_ringbuf_push(&ring, &i);
    say_pass_fail(lc_ringbuf_count(&ring) == 3);

    /* stress: many push/pop cycles */
    lc_print_string(STDOUT, S("ringbuf_stress_cycles"));
    lc_ringbuf_clear(&ring);
    ok = true;
    for (int32_t cycle = 0; cycle < 100; cycle++) {
        /* push 4, pop 4 */
        for (int32_t i = 0; i < 4; i++) {
            int32_t v = cycle * 4 + i;
            if (!lc_ringbuf_push(&ring, &v)) { ok = false; break; }
        }
        for (int32_t i = 0; i < 4; i++) {
            int32_t v;
            if (!lc_ringbuf_pop(&ring, &v)) { ok = false; break; }
            if (v != cycle * 4 + i) { ok = false; break; }
        }
        if (!ok) break;
    }
    say_pass_fail(ok && lc_ringbuf_is_empty(&ring));

    lc_ringbuf_destroy(&ring);
}

/* ========================================================================
 * List tests
 * ======================================================================== */

typedef struct {
    int32_t      value;
    lc_list_node node;
} test_item;

static void test_list(void) {
    lc_print_line(STDOUT, S("--- list ---"));

    /* init */
    lc_print_string(STDOUT, S("list_init"));
    lc_list list;
    lc_list_init(&list);
    say_pass_fail(lc_list_is_empty(&list) && lc_list_count(&list) == 0);

    /* push_back */
    lc_print_string(STDOUT, S("list_push_back"));
    test_item items[5];
    for (int32_t i = 0; i < 5; i++) items[i].value = (i + 1) * 10;
    lc_list_push_back(&list, &items[0].node);
    lc_list_push_back(&list, &items[1].node);
    lc_list_push_back(&list, &items[2].node);
    say_pass_fail(lc_list_count(&list) == 3);

    /* front and back */
    lc_print_string(STDOUT, S("list_front_back"));
    test_item *front = lc_list_entry(lc_list_front(&list), test_item, node);
    test_item *back  = lc_list_entry(lc_list_back(&list), test_item, node);
    say_pass_fail(front->value == 10 && back->value == 30);

    /* push_front */
    lc_print_string(STDOUT, S("list_push_front"));
    lc_list_push_front(&list, &items[3].node); /* 40 at front */
    front = lc_list_entry(lc_list_front(&list), test_item, node);
    say_pass_fail(front->value == 40 && lc_list_count(&list) == 4);

    /* iterate with lc_list_for_each */
    lc_print_string(STDOUT, S("list_iterate"));
    int32_t expected_order[] = {40, 10, 20, 30};
    lc_list_node *pos;
    int idx = 0;
    bool ok = true;
    lc_list_for_each(pos, &list) {
        test_item *it = lc_list_entry(pos, test_item, node);
        if (it->value != expected_order[idx]) { ok = false; break; }
        idx++;
    }
    say_pass_fail(ok && idx == 4);

    /* remove middle element */
    lc_print_string(STDOUT, S("list_remove_middle"));
    lc_list_remove(&list, &items[0].node); /* remove 10 */
    say_pass_fail(lc_list_count(&list) == 3);

    /* verify after remove */
    lc_print_string(STDOUT, S("list_verify_after_remove"));
    int32_t expected2[] = {40, 20, 30};
    idx = 0;
    ok = true;
    lc_list_for_each(pos, &list) {
        test_item *it = lc_list_entry(pos, test_item, node);
        if (it->value != expected2[idx]) { ok = false; break; }
        idx++;
    }
    say_pass_fail(ok && idx == 3);

    /* pop_front */
    lc_print_string(STDOUT, S("list_pop_front"));
    lc_list_node *popped = lc_list_pop_front(&list);
    test_item *popped_item = lc_list_entry(popped, test_item, node);
    say_pass_fail(popped_item->value == 40 && lc_list_count(&list) == 2);

    /* pop_back */
    lc_print_string(STDOUT, S("list_pop_back"));
    popped = lc_list_pop_back(&list);
    popped_item = lc_list_entry(popped, test_item, node);
    say_pass_fail(popped_item->value == 30 && lc_list_count(&list) == 1);

    /* pop until empty */
    lc_print_string(STDOUT, S("list_pop_empty"));
    lc_list_pop_front(&list);
    say_pass_fail(lc_list_is_empty(&list) && lc_list_count(&list) == 0);

    /* pop from empty list */
    lc_print_string(STDOUT, S("list_pop_from_empty"));
    say_pass_fail(lc_list_pop_front(&list) == NULL && lc_list_pop_back(&list) == NULL);

    /* front/back of empty list */
    lc_print_string(STDOUT, S("list_front_back_empty"));
    say_pass_fail(lc_list_front(&list) == NULL && lc_list_back(&list) == NULL);

    /* safe iteration with removal */
    lc_print_string(STDOUT, S("list_safe_iteration"));
    lc_list_init(&list);
    test_item safe_items[6];
    for (int32_t i = 0; i < 6; i++) {
        safe_items[i].value = i;
        lc_list_push_back(&list, &safe_items[i].node);
    }
    /* Remove all even-valued items during iteration */
    lc_list_node *tmp;
    lc_list_for_each_safe(pos, tmp, &list) {
        test_item *it = lc_list_entry(pos, test_item, node);
        if (it->value % 2 == 0) {
            lc_list_remove(&list, pos);
        }
    }
    say_pass_fail(lc_list_count(&list) == 3);

    /* verify only odd values remain */
    lc_print_string(STDOUT, S("list_verify_safe_removal"));
    int32_t expected_odd[] = {1, 3, 5};
    idx = 0;
    ok = true;
    lc_list_for_each(pos, &list) {
        test_item *it = lc_list_entry(pos, test_item, node);
        if (it->value != expected_odd[idx]) { ok = false; break; }
        idx++;
    }
    say_pass_fail(ok && idx == 3);

    /* lc_list_entry macro correctness */
    lc_print_string(STDOUT, S("list_entry_macro"));
    lc_list_init(&list);
    test_item macro_test = { .value = 12345 };
    lc_list_push_back(&list, &macro_test.node);
    lc_list_node *n = lc_list_front(&list);
    test_item *recovered = lc_list_entry(n, test_item, node);
    say_pass_fail(recovered == &macro_test && recovered->value == 12345);

    /* stress: push 100 items, remove all */
    lc_print_string(STDOUT, S("list_stress"));
    lc_list_init(&list);
    test_item stress[100];
    for (int32_t i = 0; i < 100; i++) {
        stress[i].value = i;
        lc_list_push_back(&list, &stress[i].node);
    }
    ok = lc_list_count(&list) == 100;
    /* Pop all from front */
    for (int32_t i = 0; i < 100 && ok; i++) {
        popped = lc_list_pop_front(&list);
        if (popped == NULL) { ok = false; break; }
        test_item *it = lc_list_entry(popped, test_item, node);
        if (it->value != i) { ok = false; }
    }
    say_pass_fail(ok && lc_list_is_empty(&list));
}

/* ========================================================================
 * main
 * ======================================================================== */

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    test_array();
    test_hashmap();
    test_ringbuf();
    test_list();

    lc_print_line(STDOUT, S("all passed"));
    return 0;
}
