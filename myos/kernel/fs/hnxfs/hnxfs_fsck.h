/* HNXFS consistency checker (fsck).
 *
 * Validates a superblock's magic/version/geometry and the internal ordering of
 * the on-disk regions (bitmaps, inode table, data area). Can run against an
 * in-memory superblock or read block 0 from a live block device. Reports each
 * class of problem it finds so corruption can be detected without trusting the
 * filesystem.
 */
#ifndef MYOS_HNXFS_FSCK_H
#define MYOS_HNXFS_FSCK_H

#include "types.h"
#include "hnxfs_format.h"

struct block_device;

#define HNXFS_FSCK_BAD_MAGIC     0x0001u
#define HNXFS_FSCK_BAD_VERSION   0x0002u
#define HNXFS_FSCK_BAD_BLOCKSIZE 0x0004u
#define HNXFS_FSCK_BAD_LAYOUT    0x0008u
#define HNXFS_FSCK_BAD_ROOT      0x0010u
#define HNXFS_FSCK_BAD_COUNTS    0x0020u
#define HNXFS_FSCK_READ_ERROR    0x0040u

struct hnxfs_fsck_report {
    uint32_t problems;     /* bitmask of HNXFS_FSCK_* */
    uint32_t problem_count;
    uint64_t total_blocks;
    uint64_t inode_count;
};

/* Returns 0 when clean, or a negative problem count when issues are found. */
int hnxfs_fsck_superblock(const struct hnxfs_superblock *sb,
                          struct hnxfs_fsck_report *rep);

/* Reads block 0 from `dev` and validates it. */
int hnxfs_fsck_device(struct block_device *dev, struct hnxfs_fsck_report *rep);

#endif /* MYOS_HNXFS_FSCK_H */
