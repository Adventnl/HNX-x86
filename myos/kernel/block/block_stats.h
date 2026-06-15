/* Block I/O statistics: global counters maintained by block_read/block_write.
 * Lightweight diagnostics for the storage self-tests and `block` coreutils. */
#ifndef MYOS_BLOCK_STATS_H
#define MYOS_BLOCK_STATS_H

#include "types.h"

struct block_device;

struct block_stats {
    uint64_t reads;          /* successful block_read() calls          */
    uint64_t writes;         /* successful block_write() calls         */
    uint64_t read_sectors;   /* sectors transferred by reads           */
    uint64_t write_sectors;  /* sectors transferred by writes          */
    uint64_t errors;         /* read/write calls that returned < 0     */
};

/* Copy the current global block I/O counters into `out` (out must be non-NULL).
 * Counters are accumulated across every block device. */
void block_get_stats(struct block_stats *out);

/* Reset all counters to zero (used by self-tests for a clean baseline). */
void block_stats_reset(void);

#endif /* MYOS_BLOCK_STATS_H */
