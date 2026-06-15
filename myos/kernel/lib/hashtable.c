/* Chained hash table implementation (see kernel/lib/hashtable.h). */
#include "hashtable.h"
#include "heap.h"

uint64_t hash_u64(uint64_t x) {
    /* SplitMix64 finalizer — good avalanche, cheap. */
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);
    return x;
}

uint64_t hash_bytes(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

static size_t round_pow2(size_t v) {
    size_t p = 8;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

int hashtable_init(struct hashtable *ht, size_t hint) {
    size_t n = round_pow2(hint ? hint : 8);
    ht->buckets = (struct hnode **)kcalloc(n, sizeof(struct hnode *));
    if (!ht->buckets) {
        return -1;
    }
    ht->nbuckets = n;
    ht->mask = n - 1;
    ht->count = 0;
    return 0;
}

void hashtable_destroy(struct hashtable *ht) {
    if (!ht->buckets) {
        return;
    }
    for (size_t i = 0; i < ht->nbuckets; i++) {
        struct hnode *n = ht->buckets[i];
        while (n) {
            struct hnode *next = n->next;
            kfree(n);
            n = next;
        }
    }
    kfree(ht->buckets);
    ht->buckets = NULL;
    ht->nbuckets = ht->mask = ht->count = 0;
}

static struct hnode *find_node(const struct hashtable *ht, uint64_t key,
                               size_t *bucket_out) {
    size_t b = hash_u64(key) & ht->mask;
    if (bucket_out) {
        *bucket_out = b;
    }
    struct hnode *n = ht->buckets[b];
    while (n) {
        if (n->key == key) {
            return n;
        }
        n = n->next;
    }
    return NULL;
}

int hashtable_put(struct hashtable *ht, uint64_t key, void *value) {
    size_t b;
    struct hnode *existing = find_node(ht, key, &b);
    if (existing) {
        existing->value = value;
        return 0;
    }
    struct hnode *n = (struct hnode *)kmalloc(sizeof(struct hnode));
    if (!n) {
        return -1;
    }
    n->key = key;
    n->value = value;
    n->next = ht->buckets[b];
    ht->buckets[b] = n;
    ht->count++;
    return 0;
}

void *hashtable_get(const struct hashtable *ht, uint64_t key, int *found) {
    struct hnode *n = find_node(ht, key, NULL);
    if (found) {
        *found = n ? 1 : 0;
    }
    return n ? n->value : NULL;
}

void *hashtable_remove(struct hashtable *ht, uint64_t key) {
    size_t b = hash_u64(key) & ht->mask;
    struct hnode **pp = &ht->buckets[b];
    while (*pp) {
        if ((*pp)->key == key) {
            struct hnode *victim = *pp;
            void *val = victim->value;
            *pp = victim->next;
            kfree(victim);
            ht->count--;
            return val;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

int hashtable_contains(const struct hashtable *ht, uint64_t key) {
    return find_node(ht, key, NULL) != NULL;
}

void hashtable_foreach(const struct hashtable *ht,
                       void (*fn)(uint64_t key, void *value, void *ctx),
                       void *ctx) {
    for (size_t i = 0; i < ht->nbuckets; i++) {
        struct hnode *n = ht->buckets[i];
        while (n) {
            fn(n->key, n->value, ctx);
            n = n->next;
        }
    }
}
