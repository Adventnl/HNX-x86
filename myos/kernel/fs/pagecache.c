/* Generic page cache implementation (see kernel/fs/pagecache.h). */
#include "pagecache.h"
#include "slab.h"
#include "string.h"

void pagecache_init(struct pagecache *pc, void *backing,
                    pagecache_read_fn rd, pagecache_write_fn wr,
                    uint32_t capacity, uint32_t readahead) {
    radix_init(&pc->pages);
    list_init(&pc->lru);
    pc->backing = backing;
    pc->read = rd;
    pc->write = wr;
    pc->capacity = capacity ? capacity : 16;
    pc->resident = 0;
    pc->readahead = readahead;
    pc->hits = pc->misses = pc->writebacks = pc->evictions = pc->readaheads = 0;
}

static void free_page(struct pagecache *pc, struct pagecache_page *pg) {
    if (pg->dirty && pc->write) {
        pc->write(pc->backing, pg->pgno, pg->data);
        pc->writebacks++;
    }
    kmem_free(pg->data);
    kmem_free(pg);
}

void pagecache_destroy(struct pagecache *pc) {
    struct list_node *p, *tmp;
    list_for_each_safe(p, tmp, &pc->lru) {
        struct pagecache_page *pg = list_entry(p, struct pagecache_page, lru);
        list_del_init(p);
        radix_remove(&pc->pages, pg->pgno);
        free_page(pc, pg);
    }
    radix_destroy(&pc->pages);
    pc->resident = 0;
}

static void evict_lru(struct pagecache *pc) {
    if (list_empty(&pc->lru)) {
        return;
    }
    struct pagecache_page *victim = list_last_entry(&pc->lru, struct pagecache_page, lru);
    list_del_init(&victim->lru);
    radix_remove(&pc->pages, victim->pgno);
    free_page(pc, victim);
    pc->resident--;
    pc->evictions++;
}

static struct pagecache_page *alloc_and_fill(struct pagecache *pc, uint64_t pgno) {
    while (pc->resident >= pc->capacity) {
        evict_lru(pc);
    }
    struct pagecache_page *pg = (struct pagecache_page *)kmem_zalloc(sizeof(*pg));
    if (!pg) {
        return NULL;
    }
    pg->data = (uint8_t *)kmem_alloc(PAGECACHE_PAGE_SIZE);
    if (!pg->data) {
        kmem_free(pg);
        return NULL;
    }
    pg->pgno = pgno;
    pg->dirty = 0;
    if (pc->read) {
        if (pc->read(pc->backing, pgno, pg->data) != 0) {
            memset(pg->data, 0, PAGECACHE_PAGE_SIZE);
        }
    } else {
        memset(pg->data, 0, PAGECACHE_PAGE_SIZE);
    }
    list_init(&pg->lru);
    if (radix_insert(&pc->pages, pgno, pg) != 0) {
        kmem_free(pg->data);
        kmem_free(pg);
        return NULL;
    }
    list_add(&pg->lru, &pc->lru);
    pc->resident++;
    return pg;
}

struct pagecache_page *pagecache_get(struct pagecache *pc, uint64_t pgno) {
    struct pagecache_page *pg = (struct pagecache_page *)radix_lookup(&pc->pages, pgno);
    if (pg) {
        pc->hits++;
        list_del_init(&pg->lru);
        list_add(&pg->lru, &pc->lru);   /* MRU */
        return pg;
    }
    pc->misses++;
    pg = alloc_and_fill(pc, pgno);
    /* Read-ahead the following pages (best-effort, count as prefetch). */
    for (uint32_t i = 1; i <= pc->readahead; i++) {
        uint64_t next = pgno + i;
        if (!radix_lookup(&pc->pages, next)) {
            if (alloc_and_fill(pc, next)) {
                pc->readaheads++;
            }
        }
    }
    return pg;
}

void pagecache_mark_dirty(struct pagecache *pc, uint64_t pgno) {
    struct pagecache_page *pg = (struct pagecache_page *)radix_lookup(&pc->pages, pgno);
    if (pg) {
        pg->dirty = 1;
    }
}

int pagecache_read(struct pagecache *pc, uint64_t offset, void *buf, uint64_t len) {
    uint8_t *out = (uint8_t *)buf;
    while (len > 0) {
        uint64_t pgno = offset / PAGECACHE_PAGE_SIZE;
        uint64_t poff = offset % PAGECACHE_PAGE_SIZE;
        uint64_t chunk = PAGECACHE_PAGE_SIZE - poff;
        if (chunk > len) {
            chunk = len;
        }
        struct pagecache_page *pg = pagecache_get(pc, pgno);
        if (!pg) {
            return -1;
        }
        memcpy(out, pg->data + poff, chunk);
        out += chunk;
        offset += chunk;
        len -= chunk;
    }
    return 0;
}

int pagecache_write(struct pagecache *pc, uint64_t offset, const void *buf, uint64_t len) {
    const uint8_t *in = (const uint8_t *)buf;
    while (len > 0) {
        uint64_t pgno = offset / PAGECACHE_PAGE_SIZE;
        uint64_t poff = offset % PAGECACHE_PAGE_SIZE;
        uint64_t chunk = PAGECACHE_PAGE_SIZE - poff;
        if (chunk > len) {
            chunk = len;
        }
        struct pagecache_page *pg = pagecache_get(pc, pgno);
        if (!pg) {
            return -1;
        }
        memcpy(pg->data + poff, in, chunk);
        pg->dirty = 1;
        in += chunk;
        offset += chunk;
        len -= chunk;
    }
    return 0;
}

int pagecache_sync(struct pagecache *pc) {
    int n = 0;
    struct pagecache_page *pg;
    list_for_each_entry(pg, &pc->lru, lru) {
        if (pg->dirty && pc->write) {
            pc->write(pc->backing, pg->pgno, pg->data);
            pg->dirty = 0;
            pc->writebacks++;
            n++;
        }
    }
    return n;
}

void pagecache_evict(struct pagecache *pc, uint64_t pgno) {
    struct pagecache_page *pg = (struct pagecache_page *)radix_lookup(&pc->pages, pgno);
    if (pg) {
        list_del_init(&pg->lru);
        radix_remove(&pc->pages, pgno);
        free_page(pc, pg);
        pc->resident--;
    }
}
