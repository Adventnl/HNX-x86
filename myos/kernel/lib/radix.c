/* Radix tree implementation (see kernel/lib/radix.h). */
#include "radix.h"
#include "heap.h"

struct radix_node {
    void  *slots[RADIX_FANOUT];  /* child node* (internal) or value (leaf) */
    size_t used;                 /* number of non-NULL slots               */
};

void radix_init(struct radix_tree *t) {
    t->root = NULL;
    t->count = 0;
}

static struct radix_node *node_new(void) {
    struct radix_node *n = (struct radix_node *)kcalloc(1, sizeof(struct radix_node));
    return n;
}

static void node_free_recursive(struct radix_node *n, int level) {
    if (!n) {
        return;
    }
    if (level > 0) {
        for (unsigned i = 0; i < RADIX_FANOUT; i++) {
            if (n->slots[i]) {
                node_free_recursive((struct radix_node *)n->slots[i], level - 1);
            }
        }
    }
    kfree(n);
}

void radix_destroy(struct radix_tree *t) {
    node_free_recursive(t->root, RADIX_LEVELS - 1);
    t->root = NULL;
    t->count = 0;
}

static inline unsigned slot_index(uint64_t key, int level) {
    return (unsigned)((key >> (level * RADIX_BITS)) & RADIX_MASK);
}

int radix_insert(struct radix_tree *t, uint64_t key, void *value) {
    if (key > RADIX_MAX_KEY || value == NULL) {
        return -1;
    }
    if (!t->root) {
        t->root = node_new();
        if (!t->root) {
            return -1;
        }
    }
    struct radix_node *n = t->root;
    for (int level = RADIX_LEVELS - 1; level > 0; level--) {
        unsigned idx = slot_index(key, level);
        struct radix_node *child = (struct radix_node *)n->slots[idx];
        if (!child) {
            child = node_new();
            if (!child) {
                return -1;
            }
            n->slots[idx] = child;
            n->used++;
        }
        n = child;
    }
    unsigned leaf = slot_index(key, 0);
    if (!n->slots[leaf]) {
        n->used++;
        t->count++;
    }
    n->slots[leaf] = value;
    return 0;
}

void *radix_lookup(const struct radix_tree *t, uint64_t key) {
    if (key > RADIX_MAX_KEY || !t->root) {
        return NULL;
    }
    const struct radix_node *n = t->root;
    for (int level = RADIX_LEVELS - 1; level > 0; level--) {
        n = (const struct radix_node *)n->slots[slot_index(key, level)];
        if (!n) {
            return NULL;
        }
    }
    return n->slots[slot_index(key, 0)];
}

void *radix_remove(struct radix_tree *t, uint64_t key) {
    if (key > RADIX_MAX_KEY || !t->root) {
        return NULL;
    }
    /* Track the path so we can prune empty nodes on the way back up. */
    struct radix_node *path[RADIX_LEVELS];
    unsigned idx[RADIX_LEVELS];
    struct radix_node *n = t->root;
    for (int level = RADIX_LEVELS - 1; level > 0; level--) {
        unsigned i = slot_index(key, level);
        path[level] = n;
        idx[level] = i;
        n = (struct radix_node *)n->slots[i];
        if (!n) {
            return NULL;
        }
    }
    unsigned leaf = slot_index(key, 0);
    void *value = n->slots[leaf];
    if (!value) {
        return NULL;
    }
    n->slots[leaf] = NULL;
    n->used--;
    t->count--;
    path[0] = n;
    idx[0] = leaf;

    /* Prune empty nodes bottom-up (never free the root). */
    for (int level = 0; level < RADIX_LEVELS - 1; level++) {
        struct radix_node *cur = path[level];
        if (cur->used != 0) {
            break;
        }
        struct radix_node *parent = path[level + 1];
        parent->slots[idx[level + 1]] = NULL;
        parent->used--;
        kfree(cur);
    }
    if (t->root && t->root->used == 0) {
        kfree(t->root);
        t->root = NULL;
    }
    return value;
}

static void foreach_rec(const struct radix_node *n, int level,
                        uint64_t prefix,
                        void (*fn)(uint64_t, void *, void *), void *ctx) {
    if (!n) {
        return;
    }
    for (unsigned i = 0; i < RADIX_FANOUT; i++) {
        if (!n->slots[i]) {
            continue;
        }
        uint64_t key = prefix | ((uint64_t)i << (level * RADIX_BITS));
        if (level == 0) {
            fn(key, n->slots[i], ctx);
        } else {
            foreach_rec((const struct radix_node *)n->slots[i], level - 1, key, fn, ctx);
        }
    }
}

void radix_foreach(const struct radix_tree *t,
                   void (*fn)(uint64_t key, void *value, void *ctx),
                   void *ctx) {
    foreach_rec(t->root, RADIX_LEVELS - 1, 0, fn, ctx);
}
