/* Generic page cache.
 *
 * Caches fixed 4 KiB pages of a backing object (a file or device) indexed by
 * page number. On a miss the backing read fills the page; dirty pages are
 * written back lazily (on sync, eviction, or explicit flush). Optional
 * read-ahead pulls the following pages on a miss. The backing object is
 * abstracted by a read/write callback pair so the same cache serves files,
 * block devices and anonymous memory.
 */
#ifndef MYOS_FS_PAGECACHE_H
#define MYOS_FS_PAGECACHE_H

#include "types.h"
#include "list.h"
#include "radix.h"

#define PAGECACHE_PAGE_SIZE 4096u

struct pagecache;

/* Fill `page` (PAGECACHE_PAGE_SIZE bytes) from backing storage at page index
 * `pgno`; return 0 on success. */
typedef int (*pagecache_read_fn)(void *backing, uint64_t pgno, void *page);
/* Write `page` back to backing storage at page index `pgno`; return 0. */
typedef int (*pagecache_write_fn)(void *backing, uint64_t pgno, const void *page);

struct pagecache_page {
    uint64_t         pgno;
    uint8_t         *data;
    int              dirty;
    uint64_t         refs;
    struct list_node lru;
};

struct pagecache {
    struct radix_tree   pages;       /* pgno -> struct pagecache_page* */
    struct list_node    lru;
    void               *backing;
    pagecache_read_fn   read;
    pagecache_write_fn  write;
    uint32_t            capacity;    /* max resident pages */
    uint32_t            resident;
    uint32_t            readahead;   /* pages to prefetch on a miss (0 = off) */

    /* statistics */
    uint64_t hits;
    uint64_t misses;
    uint64_t writebacks;
    uint64_t evictions;
    uint64_t readaheads;
};

void pagecache_init(struct pagecache *pc, void *backing,
                    pagecache_read_fn rd, pagecache_write_fn wr,
                    uint32_t capacity, uint32_t readahead);
void pagecache_destroy(struct pagecache *pc);

/* Get the cached page for `pgno`, filling it on a miss. NULL on failure. */
struct pagecache_page *pagecache_get(struct pagecache *pc, uint64_t pgno);

/* Read/write through the cache at an arbitrary byte offset. */
int pagecache_read(struct pagecache *pc, uint64_t offset, void *buf, uint64_t len);
int pagecache_write(struct pagecache *pc, uint64_t offset, const void *buf, uint64_t len);

/* Mark a resident page dirty. */
void pagecache_mark_dirty(struct pagecache *pc, uint64_t pgno);
/* Write all dirty pages back. Returns the number written. */
int  pagecache_sync(struct pagecache *pc);
/* Drop a clean page; if dirty it is written back first. */
void pagecache_evict(struct pagecache *pc, uint64_t pgno);

#endif /* MYOS_FS_PAGECACHE_H */
