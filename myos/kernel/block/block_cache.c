/* Direct-mapped, write-through sector cache. Each line holds one 512-byte
 * sector keyed by (device, lba). Writes go straight to the device and update the
 * cached copy, so a line is never left dirty (dirty stays 0; the counter exists
 * for the write-back design that arrives with journaling). */
#include "block_cache.h"
#include "block_device.h"
#include "string.h"
#include "log.h"

#define CACHE_LINES 64

struct cache_line {
    struct block_device *dev;
    uint64_t lba;
    uint8_t  valid;
    uint8_t  dirty;
    uint8_t  data[BLOCK_SECTOR_SIZE];
};

static struct cache_line g_lines[CACHE_LINES];
static uint64_t g_hits, g_misses, g_writes, g_evictions;

void block_cache_init(void) {
    for (int i = 0; i < CACHE_LINES; i++) {
        g_lines[i].valid = 0;
        g_lines[i].dirty = 0;
    }
    g_hits = g_misses = g_writes = g_evictions = 0;
    kernel_log_ok("Block cache online");
}

static struct cache_line *slot_for(struct block_device *dev, uint64_t lba) {
    uint64_t h = lba * 2654435761ull + ((uint64_t)(uintptr_t)dev >> 4);
    return &g_lines[h % CACHE_LINES];
}

int block_cache_read(struct block_device *dev, uint64_t lba, void *buffer) {
    struct cache_line *l = slot_for(dev, lba);
    if (l->valid && l->dev == dev && l->lba == lba) {
        g_hits++;
        memcpy(buffer, l->data, BLOCK_SECTOR_SIZE);
        return 0;
    }
    g_misses++;
    if (l->valid && (l->dev != dev || l->lba != lba)) {
        g_evictions++;
    }
    if (!dev->read || dev->read(dev, lba, l->data, 1) != 0) {
        l->valid = 0;
        return -1;
    }
    l->dev = dev;
    l->lba = lba;
    l->valid = 1;
    l->dirty = 0;
    memcpy(buffer, l->data, BLOCK_SECTOR_SIZE);
    return 0;
}

int block_cache_write(struct block_device *dev, uint64_t lba, const void *buffer) {
    g_writes++;
    if (!dev->write || dev->write(dev, lba, buffer, 1) != 0) {
        return -1;   /* write-through: hit the device first */
    }
    struct cache_line *l = slot_for(dev, lba);
    if (l->valid && (l->dev != dev || l->lba != lba)) {
        g_evictions++;
    }
    l->dev = dev;
    l->lba = lba;
    l->valid = 1;
    l->dirty = 0;            /* write-through keeps it clean */
    memcpy(l->data, buffer, BLOCK_SECTOR_SIZE);
    return 0;
}

void block_cache_flush_all(void) {
    /* Write-through: nothing dirty to flush. Present for the API + future
     * write-back mode. */
    for (int i = 0; i < CACHE_LINES; i++) {
        g_lines[i].dirty = 0;
    }
}

void block_cache_dump_stats(void) {
    kernel_log_hex64("    cache hits     : ", g_hits);
    kernel_log_hex64("    cache misses   : ", g_misses);
    kernel_log_hex64("    cache writes   : ", g_writes);
    kernel_log_hex64("    cache evictions: ", g_evictions);
    kernel_log_hex64("    cache dirty    : ", block_cache_dirty());
}

uint64_t block_cache_hits(void)      { return g_hits; }
uint64_t block_cache_misses(void)    { return g_misses; }
uint64_t block_cache_writes(void)    { return g_writes; }
uint64_t block_cache_evictions(void) { return g_evictions; }
uint64_t block_cache_dirty(void) {
    uint64_t n = 0;
    for (int i = 0; i < CACHE_LINES; i++) {
        if (g_lines[i].valid && g_lines[i].dirty) {
            n++;
        }
    }
    return n;
}
