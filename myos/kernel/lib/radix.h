/* Radix tree: 64-bit key -> void*, fixed 6-bit (64-ary) stride.
 *
 * A compact sparse map for dense-ish integer keys (page indices, inode numbers,
 * fd numbers). Six levels of 64 slots cover the low 36 bits, which is plenty
 * for the index spaces the kernel uses; higher bits are validated and rejected.
 */
#ifndef MYOS_LIB_RADIX_H
#define MYOS_LIB_RADIX_H

#include "types.h"

#define RADIX_BITS     6
#define RADIX_FANOUT   (1u << RADIX_BITS)   /* 64 */
#define RADIX_MASK     (RADIX_FANOUT - 1u)
#define RADIX_LEVELS   6                    /* covers 36-bit keys */
#define RADIX_MAX_KEY  ((1ULL << (RADIX_BITS * RADIX_LEVELS)) - 1ULL)

struct radix_node;

struct radix_tree {
    struct radix_node *root;
    size_t             count;
};

void  radix_init(struct radix_tree *t);
void  radix_destroy(struct radix_tree *t);

/* Insert/replace. Returns 0 on success, -1 on bad key or allocation failure. */
int   radix_insert(struct radix_tree *t, uint64_t key, void *value);
/* Look up; returns the value or NULL. */
void *radix_lookup(const struct radix_tree *t, uint64_t key);
/* Remove; returns the removed value or NULL. Prunes now-empty internal nodes. */
void *radix_remove(struct radix_tree *t, uint64_t key);

static inline size_t radix_count(const struct radix_tree *t) { return t->count; }

/* Iterate entries in ascending key order. */
void  radix_foreach(const struct radix_tree *t,
                    void (*fn)(uint64_t key, void *value, void *ctx),
                    void *ctx);

#endif /* MYOS_LIB_RADIX_H */
