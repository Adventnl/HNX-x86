/* HNXFS file data: 12 direct 4 KiB blocks (48 KiB max file in v0). */
#include "hnxfs_file.h"
#include "string.h"
#include "syscall_numbers.h"

int64_t hnxfs_file_read(uint64_t inode_num, void *buf, uint64_t size, uint64_t offset) {
    struct hnxfs_inode in;
    if (hnxfs_read_inode(inode_num, &in) != 0) {
        return -SYS_EIO;
    }
    if (offset >= in.size) {
        return 0;
    }
    uint64_t avail = in.size - offset;
    if (size > avail) {
        size = avail;
    }
    uint8_t *dst = (uint8_t *)buf;
    uint8_t blk[HNXFS_BLOCK_SIZE];
    uint64_t done = 0;
    while (done < size) {
        uint64_t pos = offset + done;
        uint64_t bidx = pos / HNXFS_BLOCK_SIZE;
        uint64_t within = pos % HNXFS_BLOCK_SIZE;
        uint64_t chunk = HNXFS_BLOCK_SIZE - within;
        if (chunk > size - done) {
            chunk = size - done;
        }
        if (bidx >= in.blocks || in.direct[bidx] == 0) {
            memset(dst + done, 0, chunk);     /* sparse hole */
        } else if (hnxfs_read_block(in.direct[bidx], blk) == 0) {
            memcpy(dst + done, blk + within, chunk);
        } else {
            return (int64_t)done;
        }
        done += chunk;
    }
    return (int64_t)done;
}

int64_t hnxfs_file_write(uint64_t inode_num, const void *buf, uint64_t size, uint64_t offset) {
    struct hnxfs_inode in;
    if (hnxfs_read_inode(inode_num, &in) != 0) {
        return -SYS_EIO;
    }
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t blk[HNXFS_BLOCK_SIZE];
    uint64_t done = 0;
    while (done < size) {
        uint64_t pos = offset + done;
        uint64_t bidx = pos / HNXFS_BLOCK_SIZE;
        uint64_t within = pos % HNXFS_BLOCK_SIZE;
        if (bidx >= HNXFS_DIRECT) {
            break;                            /* max file size reached */
        }
        if (in.direct[bidx] == 0) {
            uint64_t nb = hnxfs_alloc_block();
            if (nb == 0) {
                break;
            }
            in.direct[bidx] = nb;
            if (bidx + 1 > in.blocks) {
                in.blocks = bidx + 1;
            }
        }
        uint64_t chunk = HNXFS_BLOCK_SIZE - within;
        if (chunk > size - done) {
            chunk = size - done;
        }
        if (hnxfs_read_block(in.direct[bidx], blk) != 0) {
            break;
        }
        memcpy(blk + within, src + done, chunk);
        if (hnxfs_write_block(in.direct[bidx], blk) != 0) {
            break;
        }
        done += chunk;
    }
    if (offset + done > in.size) {
        in.size = offset + done;
    }
    hnxfs_write_inode(inode_num, &in);
    return (int64_t)done;
}

void hnxfs_file_truncate(uint64_t inode_num) {
    struct hnxfs_inode in;
    if (hnxfs_read_inode(inode_num, &in) != 0) {
        return;
    }
    for (uint64_t b = 0; b < in.blocks && b < HNXFS_DIRECT; b++) {
        if (in.direct[b]) {
            hnxfs_free_block(in.direct[b]);
            in.direct[b] = 0;
        }
    }
    in.blocks = 0;
    in.size = 0;
    hnxfs_write_inode(inode_num, &in);
}
