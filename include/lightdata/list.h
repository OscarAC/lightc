#ifndef LIGHTDATA_LIST_H
#define LIGHTDATA_LIST_H

#include <lightc/types.h>

/*
 * Intrusive doubly-linked list — zero allocation for list operations.
 * The list node is embedded directly in the user's struct.
 * No heap allocation for push/remove — just pointer manipulation.
 *
 *   typedef struct {
 *       int value;
 *       lc_list_node node;  // embed this in your struct
 *   } my_item;
 *
 *   lc_list list;
 *   lc_list_init(&list);
 *   my_item item = { .value = 42 };
 *   lc_list_push_back(&list, &item.node);
 *
 *   // Iterate:
 *   lc_list_node *n;
 *   lc_list_for_each(n, &list) {
 *       my_item *it = lc_list_entry(n, my_item, node);
 *       // use it->value
 *   }
 */

typedef struct lc_list_node {
    struct lc_list_node *prev;
    struct lc_list_node *next;
} lc_list_node;

typedef struct {
    lc_list_node head;  /* sentinel node */
    size_t       count;
} lc_list;

/* Initialize a list. */
void lc_list_init(lc_list *list);

/* Push to front. */
void lc_list_push_front(lc_list *list, lc_list_node *node);

/* Push to back. */
void lc_list_push_back(lc_list *list, lc_list_node *node);

/* Remove a node from whatever list it's in. */
void lc_list_remove(lc_list *list, lc_list_node *node);

/* Pop from front. Returns NULL if empty. */
lc_list_node *lc_list_pop_front(lc_list *list);

/* Pop from back. Returns NULL if empty. */
lc_list_node *lc_list_pop_back(lc_list *list);

/* Get front node (without removing). Returns NULL if empty. */
lc_list_node *lc_list_front(const lc_list *list);

/* Get back node (without removing). Returns NULL if empty. */
lc_list_node *lc_list_back(const lc_list *list);

/* Is the list empty? */
bool lc_list_is_empty(const lc_list *list);

/* Number of elements. */
size_t lc_list_count(const lc_list *list);

/* Get the containing struct from a list node.
 * Usage: lc_list_entry(node_ptr, struct_type, member_name) */
#define lc_list_entry(ptr, type, member) \
    ((type *)((uint8_t *)(ptr) - __builtin_offsetof(type, member)))

/* Iterate over all nodes. */
#define lc_list_for_each(pos, list) \
    for (pos = (list)->head.next; pos != &(list)->head; pos = pos->next)

/* Iterate safely (allows removal during iteration). */
#define lc_list_for_each_safe(pos, tmp, list) \
    for (pos = (list)->head.next, tmp = pos->next; \
         pos != &(list)->head; \
         pos = tmp, tmp = pos->next)

#endif
