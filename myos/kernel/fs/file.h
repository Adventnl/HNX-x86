/* Open-file object: a refcounted (vnode, offset, flags) tuple. File descriptors
 * in a process's fd table point at these; dup-style sharing bumps the refcount.
 * Vnodes are owned by their filesystem and are never freed here. */
#ifndef MYOS_FS_FILE_H
#define MYOS_FS_FILE_H

#include "types.h"
#include "inode.h"

struct file {
    struct vnode *vnode;
    int64_t       offset;
    int           flags;
    int           refcount;
    char          path[VFS_PATH_MAX];   /* absolute path it was opened with */
};

struct file *file_alloc(struct vnode *vn, int flags, const char *abspath);
void         file_ref(struct file *f);
void         file_unref(struct file *f);   /* frees when refcount hits 0 */

#endif /* MYOS_FS_FILE_H */
