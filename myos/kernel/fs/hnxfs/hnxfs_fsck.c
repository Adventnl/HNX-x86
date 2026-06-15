/* HNXFS fsck implementation (see kernel/fs/hnxfs/hnxfs_fsck.h). */
#include "hnxfs_fsck.h"
#include "block_device.h"
#include "string.h"

static void flag(struct hnxfs_fsck_report *rep, uint32_t bit) {
    if (!(rep->problems & bit)) {
        rep->problem_count++;
    }
    rep->problems |= bit;
}

int hnxfs_fsck_superblock(const struct hnxfs_superblock *sb,
                          struct hnxfs_fsck_report *rep) {
    memset(rep, 0, sizeof(*rep));
    rep->total_blocks = sb->total_blocks;
    rep->inode_count = sb->inode_count;

    if (sb->magic != HNXFS_MAGIC) {
        flag(rep, HNXFS_FSCK_BAD_MAGIC);
    }
    if (sb->version != HNXFS_VERSION) {
        flag(rep, HNXFS_FSCK_BAD_VERSION);
    }
    if (sb->block_size != HNXFS_BLOCK_SIZE) {
        flag(rep, HNXFS_FSCK_BAD_BLOCKSIZE);
    }
    if (sb->root_inode != HNXFS_ROOT_INODE) {
        flag(rep, HNXFS_FSCK_BAD_ROOT);
    }
    if (sb->inode_count == 0 || sb->total_blocks == 0) {
        flag(rep, HNXFS_FSCK_BAD_COUNTS);
    }

    /* Region ordering: superblock(0) < inode_bitmap < data_bitmap <
     * inode_table < data_block_start, and the data area must fit. */
    if (!(sb->inode_bitmap_block >= 1 &&
          sb->data_bitmap_block > sb->inode_bitmap_block &&
          sb->inode_table_block > sb->data_bitmap_block &&
          sb->data_block_start >= sb->inode_table_block + sb->inode_table_blocks)) {
        flag(rep, HNXFS_FSCK_BAD_LAYOUT);
    }
    if (sb->data_block_start + sb->data_block_count > sb->total_blocks) {
        flag(rep, HNXFS_FSCK_BAD_LAYOUT);
    }
    if (sb->inode_table_blocks == 0 ||
        sb->inode_table_blocks * HNXFS_INODES_PER_BLOCK < sb->inode_count) {
        flag(rep, HNXFS_FSCK_BAD_COUNTS);
    }

    return rep->problem_count ? -(int)rep->problem_count : 0;
}

int hnxfs_fsck_device(struct block_device *dev, struct hnxfs_fsck_report *rep) {
    memset(rep, 0, sizeof(*rep));
    uint8_t block[HNXFS_BLOCK_SIZE];
    /* Superblock is in block 0 = the first HNXFS_SECTORS_PER_BLOCK sectors. */
    if (block_read(dev, 0, block, HNXFS_SECTORS_PER_BLOCK) != 0) {
        flag(rep, HNXFS_FSCK_READ_ERROR);
        return -(int)rep->problem_count;
    }
    const struct hnxfs_superblock *sb = (const struct hnxfs_superblock *)block;
    return hnxfs_fsck_superblock(sb, rep);
}
