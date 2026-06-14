/* VFS node ("vnode") abstraction: the in-memory handle for a file, directory or
 * character device, independent of the backing filesystem. Each filesystem
 * (ramfs, devfs) owns its vnodes and implements struct vnode_ops. */
#ifndef MYOS_FS_INODE_H
#define MYOS_FS_INODE_H

#include "types.h"

#define VFS_NAME_MAX 128
#define VFS_PATH_MAX 256

enum vnode_type {
    VNODE_FILE    = 0,
    VNODE_DIR     = 1,
    VNODE_CHARDEV = 2,
};

struct vnode;
struct filesystem;

/* One directory entry returned by readdir / one stat result. */
struct dirent {
    char     name[VFS_NAME_MAX];
    uint64_t size;
    uint32_t type;           /* enum vnode_type */
};

struct stat {
    uint64_t size;
    uint32_t type;           /* enum vnode_type */
    uint32_t mode;           /* permission bits (advisory in v0) */
};

struct vnode_ops {
    /* Transfer at an absolute byte offset; returns bytes moved or -errno. */
    int64_t (*read)(struct vnode *vn, void *buf, uint64_t size, uint64_t offset);
    int64_t (*write)(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset);
    /* Fill the `index`-th child of a directory; returns 0 on success,
     * negative when `index` is past the end. */
    int (*readdir)(struct vnode *vn, uint64_t index, struct dirent *out);
};

struct vnode {
    enum vnode_type        type;
    uint64_t               size;
    const struct vnode_ops *ops;
    struct filesystem      *fs;
    void                   *priv;    /* fs-specific node pointer */
};

/* Thin dispatch wrappers (NULL-op safe). */
int64_t vnode_read(struct vnode *vn, void *buf, uint64_t size, uint64_t offset);
int64_t vnode_write(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset);
int     vnode_readdir(struct vnode *vn, uint64_t index, struct dirent *out);

#endif /* MYOS_FS_INODE_H */
