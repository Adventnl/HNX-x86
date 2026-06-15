/* Cached, sector-looping block read/write built on the block cache. */
#include "block_device.h"
#include "block_cache.h"
#include "block_stats.h"

/* Global block I/O counters (see block_stats.h). */
static struct block_stats g_block_stats;

int block_read(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count) {
    if (!dev) {
        g_block_stats.errors++;
        return -1;
    }
    uint8_t *p = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (block_cache_read(dev, lba + i, p + (uint64_t)i * dev->sector_size) != 0) {
            g_block_stats.errors++;
            return -1;
        }
    }
    g_block_stats.reads++;
    g_block_stats.read_sectors += count;
    return 0;
}

int block_write(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count) {
    if (!dev) {
        g_block_stats.errors++;
        return -1;
    }
    const uint8_t *p = (const uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (block_cache_write(dev, lba + i, p + (uint64_t)i * dev->sector_size) != 0) {
            g_block_stats.errors++;
            return -1;
        }
    }
    g_block_stats.writes++;
    g_block_stats.write_sectors += count;
    return 0;
}

void block_get_stats(struct block_stats *out) {
    if (!out) {
        return;
    }
    *out = g_block_stats;
}

void block_stats_reset(void) {
    g_block_stats.reads = 0;
    g_block_stats.writes = 0;
    g_block_stats.read_sectors = 0;
    g_block_stats.write_sectors = 0;
    g_block_stats.errors = 0;
}
