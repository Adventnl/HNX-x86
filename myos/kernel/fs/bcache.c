/* Buffer cache implementation (see kernel/fs/bcache.h). */
#include "bcache.h"
#include "slab.h"
#include "string.h"

void bcache_init(struct bcache *bc, void *backing, bcache_io_fn io,
                 uint32_t block_size, uint32_t capacity) {
    radix_init(&bc->index);
    list_init(&bc->lru);
    bc->backing = backing;
    bc->io = io;
    bc->block_size = block_size;
    bc->capacity = capacity ? capacity : 16;
    bc->resident = 0;
    bc->hits = bc->misses = bc->writebacks = bc->evictions = 0;
}

static void flush_buf(struct bcache *bc, struct bbuf *b) {
    if (b->dirty && bc->io) {
        bc->io(bc->backing, b->blkno, b->data, 1);
        b->dirty = 0;
        bc->writebacks++;
    }
}

static void free_buf(struct bcache *bc, struct bbuf *b) {
    flush_buf(bc, b);
    kmem_free(b->data);
    kmem_free(b);
}

void bcache_destroy(struct bcache *bc) {
    /* Free both pinned and unpinned (test teardown). */
    struct list_node *p, *tmp;
    list_for_each_safe(p, tmp, &bc->lru) {
        struct bbuf *b = list_entry(p, struct bbuf, lru);
        list_del_init(p);
        radix_remove(&bc->index, b->blkno);
        free_buf(bc, b);
    }
    bc->resident = 0;
}

/* Reclaim the least-recently-used UNPINNED buffer. */
static void reclaim(struct bcache *bc) {
    struct list_node *p, *tmp;
    /* Walk from LRU end toward MRU looking for an unpinned victim. */
    for (p = bc->lru.prev, tmp = p->prev; p != &bc->lru; p = tmp, tmp = p->prev) {
        struct bbuf *b = list_entry(p, struct bbuf, lru);
        if (b->pins == 0) {
            list_del_init(&b->lru);
            radix_remove(&bc->index, b->blkno);
            free_buf(bc, b);
            bc->resident--;
            bc->evictions++;
            return;
        }
    }
}

struct bbuf *bcache_bread(struct bcache *bc, uint64_t blkno) {
    struct bbuf *b = (struct bbuf *)radix_lookup(&bc->index, blkno);
    if (b) {
        bc->hits++;
        b->pins++;
        list_del_init(&b->lru);
        list_add(&b->lru, &bc->lru);
        return b;
    }
    bc->misses++;
    if (bc->resident >= bc->capacity) {
        reclaim(bc);
    }
    b = (struct bbuf *)kmem_zalloc(sizeof(*b));
    if (!b) {
        return NULL;
    }
    b->data = (uint8_t *)kmem_alloc(bc->block_size);
    if (!b->data) {
        kmem_free(b);
        return NULL;
    }
    b->blkno = blkno;
    if (bc->io && bc->io(bc->backing, blkno, b->data, 0) == 0) {
        b->valid = 1;
    } else {
        memset(b->data, 0, bc->block_size);
        b->valid = 1;
    }
    b->pins = 1;
    list_init(&b->lru);
    if (radix_insert(&bc->index, blkno, b) != 0) {
        kmem_free(b->data);
        kmem_free(b);
        return NULL;
    }
    list_add(&b->lru, &bc->lru);
    bc->resident++;
    return b;
}

void bcache_mark_dirty(struct bcache *bc, struct bbuf *b) {
    (void)bc;
    b->dirty = 1;
}

void bcache_brelse(struct bcache *bc, struct bbuf *b) {
    (void)bc;
    if (b->pins > 0) {
        b->pins--;
    }
}

int bcache_sync(struct bcache *bc) {
    int n = 0;
    struct bbuf *b;
    list_for_each_entry(b, &bc->lru, lru) {
        if (b->dirty) {
            flush_buf(bc, b);
            n++;
        }
    }
    return n;
}
