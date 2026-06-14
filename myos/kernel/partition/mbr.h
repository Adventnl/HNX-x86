/* Classic MBR partition-table parser. */
#ifndef MYOS_PARTITION_MBR_H
#define MYOS_PARTITION_MBR_H

struct block_device;

/* Parse the MBR at LBA 0. Registers partition devices. Returns 0 on success,
 * negative if no valid MBR (or it is a GPT protective MBR). */
int mbr_parse(struct block_device *device);

#endif /* MYOS_PARTITION_MBR_H */
