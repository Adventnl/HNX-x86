/* HNXFS bitmap allocation (one 4 KiB block each for inodes and data blocks). */
#include "hnxfs_alloc.h"
#include "string.h"

static int64_t bitmap_alloc(uint64_t bitmap_block, uint64_t limit, uint64_t first) {
    uint8_t buf[HNXFS_BLOCK_SIZE];
    if (hnxfs_read_block(bitmap_block, buf) != 0) {
        return -1;
    }
    for (uint64_t i = first; i < limit; i++) {
        if (!(buf[i >> 3] & (1u << (i & 7)))) {
            buf[i >> 3] |= (uint8_t)(1u << (i & 7));
            if (hnxfs_write_block(bitmap_block, buf) != 0) {
                return -1;
            }
            return (int64_t)i;
        }
    }
    return -1;
}

static void bitmap_free(uint64_t bitmap_block, uint64_t index) {
    uint8_t buf[HNXFS_BLOCK_SIZE];
    if (hnxfs_read_block(bitmap_block, buf) != 0) {
        return;
    }
    buf[index >> 3] &= (uint8_t)~(1u << (index & 7));
    hnxfs_write_block(bitmap_block, buf);
}

uint64_t hnxfs_alloc_inode(void) {
    int64_t i = bitmap_alloc(g_hnxfs.sb.inode_bitmap_block, g_hnxfs.sb.inode_count, 2);
    return (i < 0) ? 0 : (uint64_t)i;     /* 0 and 1 are reserved (1 = root) */
}

void hnxfs_free_inode(uint64_t num) {
    if (num >= 2) {
        bitmap_free(g_hnxfs.sb.inode_bitmap_block, num);
    }
}

uint64_t hnxfs_alloc_block(void) {
    int64_t i = bitmap_alloc(g_hnxfs.sb.data_bitmap_block, g_hnxfs.sb.data_block_count, 0);
    if (i < 0) {
        return 0;
    }
    uint64_t absblk = g_hnxfs.sb.data_block_start + (uint64_t)i;
    /* Zero the freshly allocated data block via a shared, never-written zero
     * page (keeps it off the kernel stack — 4 KiB buffers nest deeply here). */
    static const uint8_t g_zero_block[HNXFS_BLOCK_SIZE];
    hnxfs_write_block(absblk, g_zero_block);
    return absblk;
}

void hnxfs_free_block(uint64_t absblk) {
    if (absblk < g_hnxfs.sb.data_block_start) {
        return;
    }
    bitmap_free(g_hnxfs.sb.data_bitmap_block, absblk - g_hnxfs.sb.data_block_start);
}
