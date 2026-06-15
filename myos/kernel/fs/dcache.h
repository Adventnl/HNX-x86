/* Directory-entry (name) cache.
 *
 * Maps absolute path strings to resolved vnodes so repeated lookups of the same
 * path skip the per-component filesystem walk. Entries are kept in a hash table
 * keyed by the FNV-1a hash of the path, with an LRU list for eviction once the
 * cache reaches its capacity. Negative caching (remembering misses) is also
 * supported to short-circuit repeated lookups of nonexistent paths.
 */
#ifndef MYOS_FS_DCACHE_H
#define MYOS_FS_DCACHE_H

#include "types.h"
#include "list.h"

struct vnode;

struct dentry {
    char             path[256];
    struct vnode    *vnode;       /* NULL for a negative entry */
    int              negative;
    uint64_t         hash;
    uint64_t         hits;
    struct list_node lru;         /* global LRU list link */
};

void dcache_init(uint32_t capacity);

/* Look up a path. Returns the cached vnode (or NULL). *is_negative is set when
 * the path is cached as a known miss. *found distinguishes a cache hit from a
 * cache miss. */
struct vnode *dcache_lookup(const char *path, int *found, int *is_negative);

/* Insert a positive (vnode) or negative (vnode==NULL) entry. */
void dcache_insert(const char *path, struct vnode *vnode);
void dcache_insert_negative(const char *path);

/* Invalidate one path (e.g. after unlink/rename) or the whole cache. */
void dcache_invalidate(const char *path);
void dcache_flush(void);

struct dcache_stats {
    uint64_t lookups;
    uint64_t hits;
    uint64_t misses;
    uint64_t inserts;
    uint64_t evictions;
    uint32_t entries;
    uint32_t capacity;
};
void dcache_get_stats(struct dcache_stats *out);

#endif /* MYOS_FS_DCACHE_H */
