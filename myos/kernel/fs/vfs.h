/* Virtual filesystem switch: a small mount table over pluggable filesystems
 * (ramfs, devfs). Path resolution finds the longest matching mount prefix and
 * delegates the remainder to that filesystem's lookup. fd-based operations act
 * on the current process's file-descriptor table. */
#ifndef MYOS_FS_VFS_H
#define MYOS_FS_VFS_H

#include "types.h"
#include "inode.h"

struct filesystem {
    const char *name;
    /* Resolve a path relative to this fs's root ("" = root). Returns a vnode
     * owned by the filesystem, or NULL if absent. */
    struct vnode *(*lookup)(struct filesystem *fs, const char *rel_path);
    void *data;            /* fs-private state */
};

#define VFS_MAX_MOUNTS 8

void vfs_init(void);

/* Mount `fs` at absolute `path` ("/" or "/dev"). `data` is stored in fs->data
 * if non-NULL. Returns 0 or negative error. */
int vfs_mount(const char *path, struct filesystem *fs, void *data);

/* Kernel-side resolve of an absolute, already-normalized path to a vnode. */
struct vnode *vfs_resolve(const char *abspath);

/* fd-based API (operate on the current process). */
int     vfs_open(const char *path, int flags);
int     vfs_close(int fd);
int64_t vfs_read(int fd, void *buffer, uint64_t size);
int64_t vfs_write(int fd, const void *buffer, uint64_t size);
int64_t vfs_lseek(int fd, int64_t offset, int whence);
int     vfs_readdir(int fd, struct dirent *out);   /* 1 = entry, 0 = end, <0 err */
int     vfs_stat(const char *path, struct stat *out);

/* Namespace mutation (writable filesystems). */
int     vfs_mkdir(const char *path);
int     vfs_create(const char *path);
int     vfs_unlink(const char *path);

/* Extended operations (kernel/fs/vfs_ops.c). rename/link are content-copy
 * foundations over the vnode primitives; truncate shrinks or zero-extends. */
int     vfs_rename(const char *oldpath, const char *newpath);
int     vfs_link(const char *oldpath, const char *newpath);
int     vfs_truncate(const char *path, uint64_t length);

/* Mount table introspection (for the `mounts` coreutil). */
int     vfs_mount_count(void);
int     vfs_mount_info(int index, char *path_out, uint64_t path_size,
                       char *fs_out, uint64_t fs_size);

#endif /* MYOS_FS_VFS_H */
