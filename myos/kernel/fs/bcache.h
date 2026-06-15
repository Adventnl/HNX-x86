/* Buffer cache: pinned, block-granular caching over a backing device.
 *
 * Where the page cache is offset-oriented and copies data in/out, the buffer
 * cache hands out pinned pointers to fixed-size block buffers (the classic
 * bread/bwrite/brelse model). A buffer stays resident while pinned; releasing
 * it returns it to the LRU for reuse. Dirty buffers are written back on sync or
 * when reclaimed. Used by filesystem metadata paths that want to read, mutate
 * in place, and write back a single block.
 */
#ifndef MYOS_FS_BCACHE_H
#define MYOS_FS_BCACHE_H

#include "types.h"
#include "list.h"
#include "radix.h"

struct bcache;

typedef int (*bcache_io_fn)(void *backing, uint64_t blkno, void *buf, int write);

struct bbuf {
    uint64_t         blkno;
    uint8_t         *data;
    int              dirty;
    int              valid;
    uint32_t         pins;
    struct list_node lru;
};

struct bcache {
    struct radix_tree index;       /* blkno -> struct bbuf* */
    struct list_node  lru;         /* unpinned buffers, MRU..LRU */
    void             *backing;
    bcache_io_fn      io;
    uint32_t          block_size;
    uint32_t          capacity;
    uint32_t          resident;

    uint64_t hits;
    uint64_t misses;
    uint64_t writebacks;
    uint64_t evictions;
};

void  bcache_init(struct bcache *bc, void *backing, bcache_io_fn io,
                  uint32_t block_size, uint32_t capacity);
void  bcache_destroy(struct bcache *bc);

/* Acquire a pinned buffer for `blkno` (filled from backing on a miss). */
struct bbuf *bcache_bread(struct bcache *bc, uint64_t blkno);
/* Mark a pinned buffer dirty. */
void  bcache_mark_dirty(struct bcache *bc, struct bbuf *b);
/* Release a pinned buffer back to the LRU. */
void  bcache_brelse(struct bcache *bc, struct bbuf *b);
/* Write every dirty buffer back; returns the count written. */
int   bcache_sync(struct bcache *bc);

#endif /* MYOS_FS_BCACHE_H */
