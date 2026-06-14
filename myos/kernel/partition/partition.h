/* Partition layer: parse MBR/GPT on each whole-disk block device and register a
 * child block device per partition (disk0p1, disk0p2, ...). */
#ifndef MYOS_PARTITION_H
#define MYOS_PARTITION_H

#include "types.h"

struct block_device;

struct partition_info {
    struct block_device *parent;
    uint64_t lba_offset;
    uint64_t lba_count;
    uint8_t  type;
};

void partition_init(void);          /* logs "[OK] Partition parser online" */
void partition_scan_all(void);      /* scan every whole disk */
int  mbr_parse(struct block_device *device);
int  gpt_parse(struct block_device *device);
void partition_dump_all(void);

/* Register a partition child device named "<parent>pN". Returns 0 on success. */
int partition_register(struct block_device *parent, int index,
                       uint64_t lba_offset, uint64_t lba_count, uint8_t type);

#endif /* MYOS_PARTITION_H */
