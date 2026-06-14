/* Partition device wrappers + whole-disk scan. */
#include "partition.h"
#include "mbr.h"
#include "gpt.h"
#include "block_registry.h"
#include "block_device.h"
#include "heap.h"
#include "string.h"
#include "log.h"

/* A partition block device forwards raw transfers to the parent's driver op with
 * a fixed LBA offset (the block cache sits above, keyed by the partition dev). */
static int part_read(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count) {
    struct partition_info *pi = (struct partition_info *)dev->driver_data;
    if (lba + count > pi->lba_count) {
        return -1;
    }
    return pi->parent->read(pi->parent, pi->lba_offset + lba, buffer, count);
}

static int part_write(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count) {
    struct partition_info *pi = (struct partition_info *)dev->driver_data;
    if (lba + count > pi->lba_count) {
        return -1;
    }
    return pi->parent->write(pi->parent, pi->lba_offset + lba, buffer, count);
}

int partition_register(struct block_device *parent, int index,
                       uint64_t lba_offset, uint64_t lba_count, uint8_t type) {
    struct block_device *dev = (struct block_device *)kcalloc(1, sizeof(*dev));
    struct partition_info *pi = (struct partition_info *)kcalloc(1, sizeof(*pi));
    if (!dev || !pi) {
        return -1;
    }
    pi->parent = parent;
    pi->lba_offset = lba_offset;
    pi->lba_count = lba_count;
    pi->type = type;

    /* "<parent>pN" */
    strlcpy(dev->name, parent->name, sizeof(dev->name));
    uint64_t n = strlen(dev->name);
    if (n + 2 < sizeof(dev->name)) {
        dev->name[n++] = 'p';
        dev->name[n++] = (char)('0' + index);
        dev->name[n] = 0;
    }
    dev->sector_count = lba_count;
    dev->sector_size = parent->sector_size;
    dev->driver_data = pi;
    dev->read = part_read;
    dev->write = part_write;
    return block_register_device(dev);
}

void partition_init(void) {
    kernel_log_ok("Partition parser online");
}

void partition_scan_all(void) {
    /* Snapshot the whole-disk devices present before we add partition children. */
    int n = block_device_count();
    struct block_device *disks[16];
    int ndisks = 0;
    for (int i = 0; i < n && ndisks < 16; i++) {
        struct block_device *d = block_device_at(i);
        /* Only scan whole disks (name has no 'p<digit>' suffix). */
        if (d && !strchr(d->name, 'p')) {
            disks[ndisks++] = d;
        }
    }
    for (int i = 0; i < ndisks; i++) {
        if (gpt_parse(disks[i]) != 0) {
            mbr_parse(disks[i]);
        }
    }
}

void partition_dump_all(void) {
    int n = block_device_count();
    for (int i = 0; i < n; i++) {
        struct block_device *d = block_device_at(i);
        if (d && strchr(d->name, 'p')) {
            kernel_log("    partition ");
            kernel_log(d->name);
            kernel_log_hex64("  sectors=", d->sector_count);
        }
    }
}
