/* HNXFS in-kernel driver: mount a block device, expose it through the VFS. */
#ifndef MYOS_HNXFS_H
#define MYOS_HNXFS_H

#include "types.h"
#include "hnxfs_format.h"
#include "vfs.h"

struct block_device;

#define HNXFS_VNODE_CACHE 64

struct hnxfs {
    struct block_device     *dev;
    struct hnxfs_superblock  sb;
    struct filesystem        fs;
    int                      mounted;
    struct vnode             vnodes[HNXFS_VNODE_CACHE];
    uint64_t                 vnode_inode[HNXFS_VNODE_CACHE];
    int                      vnode_count;
};

extern struct hnxfs g_hnxfs;

/* Mount `dev` and return its filesystem (NULL on bad/absent superblock). */
struct filesystem *hnxfs_mount(struct block_device *dev);

/* 4 KiB block I/O helpers (8 sectors each). */
int hnxfs_read_block(uint64_t fsblk, void *buf);
int hnxfs_write_block(uint64_t fsblk, const void *buf);

/* Get (or create) the cached vnode for an inode number. */
struct vnode *hnxfs_get_vnode(uint64_t inode_num);

/* inode table (hnxfs_inode.c) */
int hnxfs_read_inode(uint64_t num, struct hnxfs_inode *out);
int hnxfs_write_inode(uint64_t num, const struct hnxfs_inode *in);

/* allocation bitmaps (hnxfs_alloc.c) */
uint64_t hnxfs_alloc_inode(void);
void     hnxfs_free_inode(uint64_t num);
uint64_t hnxfs_alloc_block(void);     /* returns absolute block number */
void     hnxfs_free_block(uint64_t absblk);

/* directories (hnxfs_dir.c) */
uint64_t hnxfs_dir_lookup(uint64_t dir_inode, const char *name);
int      hnxfs_dir_add(uint64_t dir_inode, const char *name, uint64_t child);
int      hnxfs_dir_remove(uint64_t dir_inode, const char *name);
int      hnxfs_dir_get(uint64_t dir_inode, uint64_t index, struct hnxfs_dirent *out);
int      hnxfs_dir_empty(uint64_t dir_inode);

/* file data (hnxfs_file.c) */
int64_t  hnxfs_file_read(uint64_t inode_num, void *buf, uint64_t size, uint64_t offset);
int64_t  hnxfs_file_write(uint64_t inode_num, const void *buf, uint64_t size, uint64_t offset);
void     hnxfs_file_truncate(uint64_t inode_num);

#endif /* MYOS_HNXFS_H */
