#ifndef LIGHTDATA_HASHMAP_H
#define LIGHTDATA_HASHMAP_H

#include <lightc/types.h>

/*
 * Hash map — open addressing with Robin Hood hashing.
 * Cache-friendly: entries stored in a flat array (no pointer chasing).
 * Keys are strings (null-terminated). Values are void* (user-managed).
 *
 *   lc_hashmap map = lc_hashmap_create();
 *   lc_hashmap_set(&map, "name", "lightc");
 *   void *val = lc_hashmap_get(&map, "name");  // → "lightc"
 *   lc_hashmap_destroy(&map);
 */

typedef struct {
    char    *key;       /* heap-allocated copy of key string, or NULL if empty */
    void    *value;     /* user pointer */
    uint32_t hash;      /* cached hash */
    uint32_t distance;  /* Robin Hood: probe distance from ideal position */
} lc_hashmap_entry;

typedef struct {
    lc_hashmap_entry *entries;   /* flat array of entries */
    size_t            capacity;  /* total slots */
    size_t            count;     /* occupied slots */
} lc_hashmap;

lc_hashmap lc_hashmap_create(void);
void lc_hashmap_destroy(lc_hashmap *map);

/* Set a key-value pair. Overwrites if key exists. */
[[nodiscard]] lc_result lc_hashmap_set(lc_hashmap *map, const char *key, void *value);

/* Get value by key. Returns NULL if not found. */
void *lc_hashmap_get(const lc_hashmap *map, const char *key);

/* Remove a key. Returns the removed value, or NULL if not found. */
void *lc_hashmap_remove(lc_hashmap *map, const char *key);

/* Check if key exists. */
bool lc_hashmap_contains(const lc_hashmap *map, const char *key);

/* Get number of entries. */
size_t lc_hashmap_count(const lc_hashmap *map);

/* Iterate: call func(key, value, user_data) for each entry.
 * Return false from func to stop early. */
void lc_hashmap_iterate(const lc_hashmap *map,
                        bool (*func)(const char *key, void *value, void *user_data),
                        void *user_data);

#endif
