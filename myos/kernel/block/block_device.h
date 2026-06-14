/* Block device abstraction: fixed-size sector storage with read/write ops.
 * Drivers (AHCI, NVMe, partitions) fill in the ops + geometry and register. */
#ifndef MYOS_BLOCK_DEVICE_H
#define MYOS_BLOCK_DEVICE_H

#include "types.h"

#define BLOCK_SECTOR_SIZE 512u
#define BLOCK_NAME_MAX    32

struct block_device {
    char     name[BLOCK_NAME_MAX];
    uint64_t sector_count;
    uint32_t sector_size;          /* 512 */
    void    *driver_data;

    /* Transfer `count` sectors at `lba`. Return 0 on success, negative on error.
     * `buffer` is a kernel buffer; drivers handle DMA internally. */
    int (*read)(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count);
    int (*write)(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count);

    struct block_device *next;
};

/* Cached, sector-looping read/write (route through the block cache). */
int block_read(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count);
int block_write(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count);

#endif /* MYOS_BLOCK_DEVICE_H */
