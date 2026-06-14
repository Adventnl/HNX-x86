/* Cached, sector-looping block read/write built on the block cache. */
#include "block_device.h"
#include "block_cache.h"

int block_read(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count) {
    if (!dev) {
        return -1;
    }
    uint8_t *p = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (block_cache_read(dev, lba + i, p + (uint64_t)i * dev->sector_size) != 0) {
            return -1;
        }
    }
    return 0;
}

int block_write(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count) {
    if (!dev) {
        return -1;
    }
    const uint8_t *p = (const uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (block_cache_write(dev, lba + i, p + (uint64_t)i * dev->sector_size) != 0) {
            return -1;
        }
    }
    return 0;
}
