/* HNXFS1 on-disk format. A compact custom filesystem (not ext/FAT/etc.):
 *
 *   block 0                : superblock
 *   inode_bitmap_block     : 1 block, bit i = inode i in use
 *   data_bitmap_block      : 1 block, bit j = data block j in use
 *   inode_table_block ...  : inode table (32 inodes / 4 KiB block)
 *   data_block_start ...   : file/dir data blocks
 *
 * 4 KiB blocks = 8 x 512-byte sectors. Files use 12 direct block pointers
 * (48 KiB max). Directories store fixed 128-byte entries (32 / block). Both the
 * Python formatter (tools/fs/mkhnxfs.py) and the kernel driver use this layout. */
#ifndef MYOS_HNXFS_FORMAT_H
#define MYOS_HNXFS_FORMAT_H

#include "types.h"

#define HNXFS_MAGIC       0x315346584E48ULL   /* "HNXFS1" */
#define HNXFS_VERSION     1u
#define HNXFS_BLOCK_SIZE  4096u
#define HNXFS_SECTORS_PER_BLOCK (HNXFS_BLOCK_SIZE / 512u)
#define HNXFS_DIRECT      12
#define HNXFS_NAME_MAX    120
#define HNXFS_ROOT_INODE  1
#define HNXFS_INODE_SIZE  128
#define HNXFS_INODES_PER_BLOCK (HNXFS_BLOCK_SIZE / HNXFS_INODE_SIZE)
#define HNXFS_DIRENTS_PER_BLOCK (HNXFS_BLOCK_SIZE / 128u)

enum {
    HNXFS_TYPE_FREE = 0,
    HNXFS_TYPE_FILE = 1,
    HNXFS_TYPE_DIR  = 2,
};

struct hnxfs_superblock {
    uint64_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_block;
    uint64_t data_bitmap_block;
    uint64_t inode_table_block;
    uint64_t inode_table_blocks;
    uint64_t data_block_start;
    uint64_t data_block_count;
    uint64_t root_inode;
};

struct hnxfs_inode {
    uint32_t type;                  /* HNXFS_TYPE_* */
    uint32_t mode;
    uint64_t size;                  /* bytes (file) / entries-area bytes (dir) */
    uint64_t blocks;                /* allocated data block count */
    uint64_t direct[HNXFS_DIRECT];  /* absolute block numbers */
    uint64_t mtime;                 /* reserved */
};

struct hnxfs_dirent {
    uint64_t inode;                 /* 0 = empty slot */
    char     name[HNXFS_NAME_MAX];
};

#endif /* MYOS_HNXFS_FORMAT_H */
