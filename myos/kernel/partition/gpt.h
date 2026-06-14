/* GPT partition-table parser (LBA1 header + entry array). */
#ifndef MYOS_PARTITION_GPT_H
#define MYOS_PARTITION_GPT_H

struct block_device;

/* Parse a GPT. Returns 0 on success (partitions registered), negative if the
 * device is not GPT-formatted. */
int gpt_parse(struct block_device *device);

#endif /* MYOS_PARTITION_GPT_H */
