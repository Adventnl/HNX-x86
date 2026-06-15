/* Chained hash table: 64-bit key -> void* value.
 *
 * Buckets are a kmalloc'd array of singly-linked node chains. Sized at create
 * time (rounded up to a power of two so the index is a mask, not a modulo).
 * Suitable for inode caches, pid->process maps, handle tables, etc.
 */
#ifndef MYOS_LIB_HASHTABLE_H
#define MYOS_LIB_HASHTABLE_H

#include "types.h"

struct hnode {
    uint64_t      key;
    void         *value;
    struct hnode *next;
};

struct hashtable {
    struct hnode **buckets;
    size_t         nbuckets;   /* power of two */
    size_t         mask;
    size_t         count;
};

/* Create with at least `hint` buckets (rounded up to a power of two, min 8).
 * Returns 0 on success, -1 on allocation failure. */
int  hashtable_init(struct hashtable *ht, size_t hint);
void hashtable_destroy(struct hashtable *ht);

/* Insert or replace. Returns 0 on success, -1 on allocation failure. When a
 * key already exists its value is overwritten and *old (if non-NULL) receives
 * the previous value. */
int  hashtable_put(struct hashtable *ht, uint64_t key, void *value);

/* Look up; returns the value or NULL if absent. `found` (optional) disambiguates
 * a stored NULL value from a miss. */
void *hashtable_get(const struct hashtable *ht, uint64_t key, int *found);

/* Remove a key. Returns the removed value, or NULL if absent. */
void *hashtable_remove(struct hashtable *ht, uint64_t key);

int  hashtable_contains(const struct hashtable *ht, uint64_t key);
static inline size_t hashtable_count(const struct hashtable *ht) { return ht->count; }

/* Iterate every entry, calling fn(key, value, ctx). */
void hashtable_foreach(const struct hashtable *ht,
                       void (*fn)(uint64_t key, void *value, void *ctx),
                       void *ctx);

/* The integer hash used internally (exposed for tests/diagnostics). */
uint64_t hash_u64(uint64_t x);
/* FNV-1a over a byte range (string keys). */
uint64_t hash_bytes(const void *data, size_t len);

#endif /* MYOS_LIB_HASHTABLE_H */
