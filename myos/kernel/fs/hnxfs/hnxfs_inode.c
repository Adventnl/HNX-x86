/* HNXFS inode table access. */
#include "hnxfs.h"
#include "string.h"
#include "syscall_numbers.h"

int hnxfs_read_inode(uint64_t num, struct hnxfs_inode *out) {
    if (num == 0 || num >= g_hnxfs.sb.inode_count) {
        return -SYS_EINVAL;
    }
    uint64_t blk = g_hnxfs.sb.inode_table_block + num / HNXFS_INODES_PER_BLOCK;
    uint64_t idx = num % HNXFS_INODES_PER_BLOCK;
    uint8_t buf[HNXFS_BLOCK_SIZE];
    if (hnxfs_read_block(blk, buf) != 0) {
        return -SYS_EIO;
    }
    memcpy(out, buf + idx * HNXFS_INODE_SIZE, sizeof(struct hnxfs_inode));
    return 0;
}

int hnxfs_write_inode(uint64_t num, const struct hnxfs_inode *in) {
    if (num == 0 || num >= g_hnxfs.sb.inode_count) {
        return -SYS_EINVAL;
    }
    uint64_t blk = g_hnxfs.sb.inode_table_block + num / HNXFS_INODES_PER_BLOCK;
    uint64_t idx = num % HNXFS_INODES_PER_BLOCK;
    uint8_t buf[HNXFS_BLOCK_SIZE];
    if (hnxfs_read_block(blk, buf) != 0) {
        return -SYS_EIO;
    }
    memcpy(buf + idx * HNXFS_INODE_SIZE, in, sizeof(struct hnxfs_inode));
    return hnxfs_write_block(blk, buf) == 0 ? 0 : -SYS_EIO;
}
