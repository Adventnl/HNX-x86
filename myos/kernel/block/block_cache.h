/* Write-through, direct-mapped sector cache in front of block devices. */
#ifndef MYOS_BLOCK_CACHE_H
#define MYOS_BLOCK_CACHE_H

#include "types.h"

struct block_device;

void block_cache_init(void);

/* One-sector cached read/write. Return 0 on success. */
int  block_cache_read(struct block_device *dev, uint64_t lba, void *buffer);
int  block_cache_write(struct block_device *dev, uint64_t lba, const void *buffer);

void block_cache_flush_all(void);
void block_cache_dump_stats(void);

/* Stats (for the cache self-test). */
uint64_t block_cache_hits(void);
uint64_t block_cache_misses(void);
uint64_t block_cache_writes(void);
uint64_t block_cache_evictions(void);
uint64_t block_cache_dirty(void);

#endif /* MYOS_BLOCK_CACHE_H */
