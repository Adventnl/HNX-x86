/* Dentry / name cache implementation (see kernel/fs/dcache.h). */
#include "dcache.h"
#include "inode.h"
#include "hashtable.h"
#include "slab.h"
#include "string.h"

static struct hashtable g_index;       /* path-hash -> struct dentry* */
static struct list_node g_lru;         /* MRU at head, LRU at tail */
static struct kmem_cache *g_dentry_cache;
static uint32_t g_capacity;
static uint32_t g_entries;
static struct dcache_stats g_stats;
static int g_inited;

void dcache_init(uint32_t capacity) {
    hashtable_init(&g_index, capacity * 2);
    list_init(&g_lru);
    g_dentry_cache = kmem_cache_create("dentry", sizeof(struct dentry), 16, 0);
    g_capacity = capacity ? capacity : 64;
    g_entries = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.capacity = g_capacity;
    g_inited = 1;
}

static uint64_t path_hash(const char *path) {
    return hash_bytes(path, strlen(path));
}

static struct dentry *find_entry(const char *path, uint64_t h) {
    int found = 0;
    struct dentry *d = (struct dentry *)hashtable_get(&g_index, h, &found);
    /* Guard against hash collisions: confirm the path matches. */
    if (found && d && strcmp(d->path, path) == 0) {
        return d;
    }
    return NULL;
}

static void evict_one(void) {
    if (list_empty(&g_lru)) {
        return;
    }
    struct dentry *victim = list_last_entry(&g_lru, struct dentry, lru);
    list_del_init(&victim->lru);
    hashtable_remove(&g_index, victim->hash);
    kmem_cache_free(g_dentry_cache, victim);
    g_entries--;
    g_stats.evictions++;
}

static void touch(struct dentry *d) {
    list_del_init(&d->lru);
    list_add(&d->lru, &g_lru);   /* move to MRU */
}

struct vnode *dcache_lookup(const char *path, int *found, int *is_negative) {
    if (found) *found = 0;
    if (is_negative) *is_negative = 0;
    if (!g_inited) {
        return NULL;
    }
    g_stats.lookups++;
    uint64_t h = path_hash(path);
    struct dentry *d = find_entry(path, h);
    if (!d) {
        g_stats.misses++;
        return NULL;
    }
    g_stats.hits++;
    d->hits++;
    touch(d);
    if (found) *found = 1;
    if (is_negative) *is_negative = d->negative;
    return d->vnode;
}

static void insert_common(const char *path, struct vnode *vnode, int negative) {
    if (!g_inited) {
        return;
    }
    uint64_t h = path_hash(path);
    struct dentry *existing = find_entry(path, h);
    if (existing) {
        existing->vnode = vnode;
        existing->negative = negative;
        touch(existing);
        return;
    }
    while (g_entries >= g_capacity) {
        evict_one();
    }
    struct dentry *d = (struct dentry *)kmem_cache_alloc(g_dentry_cache);
    if (!d) {
        return;
    }
    strlcpy(d->path, path, sizeof(d->path));
    d->vnode = vnode;
    d->negative = negative;
    d->hash = h;
    d->hits = 0;
    list_init(&d->lru);
    if (hashtable_put(&g_index, h, d) != 0) {
        kmem_cache_free(g_dentry_cache, d);
        return;
    }
    list_add(&d->lru, &g_lru);
    g_entries++;
    g_stats.inserts++;
    g_stats.entries = g_entries;
}

void dcache_insert(const char *path, struct vnode *vnode) {
    insert_common(path, vnode, 0);
}

void dcache_insert_negative(const char *path) {
    insert_common(path, NULL, 1);
}

void dcache_invalidate(const char *path) {
    if (!g_inited) {
        return;
    }
    uint64_t h = path_hash(path);
    struct dentry *d = find_entry(path, h);
    if (d) {
        list_del_init(&d->lru);
        hashtable_remove(&g_index, h);
        kmem_cache_free(g_dentry_cache, d);
        g_entries--;
        g_stats.entries = g_entries;
    }
}

void dcache_flush(void) {
    if (!g_inited) {
        return;
    }
    struct list_node *p, *tmp;
    list_for_each_safe(p, tmp, &g_lru) {
        struct dentry *d = list_entry(p, struct dentry, lru);
        list_del_init(p);
        hashtable_remove(&g_index, d->hash);
        kmem_cache_free(g_dentry_cache, d);
    }
    g_entries = 0;
    g_stats.entries = 0;
}

void dcache_get_stats(struct dcache_stats *out) {
    *out = g_stats;
    out->entries = g_entries;
}
