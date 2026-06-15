/* Extended VFS operations: rename, link (foundation) and truncate.
 *
 * These are implemented generically over the existing vnode read/write/create/
 * unlink primitives so they work on any writable filesystem (currently HNXFS).
 * rename and the link foundation copy file contents to the new name; true
 * in-place directory-entry rename and hard links (shared inodes with a link
 * count) are a filesystem-level milestone documented in docs/vfs_deep.md. */
#include "vfs.h"
#include "inode.h"
#include "dcache.h"
#include "slab.h"
#include "string.h"
#include "syscall_numbers.h"

#define VOPS_CHUNK 4096

/* Copy the whole content of `src` into the already-created `dst`. */
static int copy_vnode(struct vnode *src, struct vnode *dst) {
    uint8_t *buf = (uint8_t *)kmem_alloc(VOPS_CHUNK);
    if (!buf) {
        return -SYS_ENOMEM;
    }
    uint64_t off = 0;
    uint64_t size = src->size;
    while (off < size) {
        uint64_t n = size - off;
        if (n > VOPS_CHUNK) {
            n = VOPS_CHUNK;
        }
        int64_t r = vnode_read(src, buf, n, off);
        if (r <= 0) {
            break;
        }
        int64_t w = vnode_write(dst, buf, (uint64_t)r, off);
        if (w != r) {
            kmem_free(buf);
            return -SYS_EIO;
        }
        off += (uint64_t)r;
    }
    kmem_free(buf);
    return 0;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    struct vnode *src = vfs_resolve(oldpath);
    if (!src) {
        return -SYS_ENOENT;
    }
    if (src->type != VNODE_FILE) {
        return -SYS_EISDIR;     /* directory rename not supported in v0 */
    }
    /* Replace any existing destination. */
    if (vfs_resolve(newpath)) {
        vfs_unlink(newpath);
        dcache_invalidate(newpath);
    }
    if (vfs_create(newpath) != 0) {
        return -SYS_EIO;
    }
    struct vnode *dst = vfs_resolve(newpath);
    if (!dst) {
        return -SYS_EIO;
    }
    int rc = copy_vnode(src, dst);
    if (rc != 0) {
        return rc;
    }
    vfs_unlink(oldpath);
    dcache_invalidate(oldpath);
    dcache_invalidate(newpath);
    return 0;
}

int vfs_link(const char *oldpath, const char *newpath) {
    /* Foundation: a content copy under a second name. True hard links require
     * inode sharing + a link count in the backing filesystem. */
    struct vnode *src = vfs_resolve(oldpath);
    if (!src) {
        return -SYS_ENOENT;
    }
    if (src->type != VNODE_FILE) {
        return -SYS_EPERM;
    }
    if (vfs_resolve(newpath)) {
        return -SYS_EEXIST;
    }
    if (vfs_create(newpath) != 0) {
        return -SYS_EIO;
    }
    struct vnode *dst = vfs_resolve(newpath);
    if (!dst) {
        return -SYS_EIO;
    }
    return copy_vnode(src, dst);
}

int vfs_truncate(const char *path, uint64_t length) {
    struct vnode *vn = vfs_resolve(path);
    if (!vn) {
        return -SYS_ENOENT;
    }
    if (vn->type != VNODE_FILE) {
        return -SYS_EISDIR;
    }
    uint64_t old = vn->size;
    if (length == old) {
        return 0;
    }
    uint64_t keep = length < old ? length : old;
    uint8_t *buf = NULL;
    if (keep) {
        buf = (uint8_t *)kmem_alloc(keep);
        if (!buf) {
            return -SYS_ENOMEM;
        }
        /* Read the prefix we want to retain (chunked). */
        uint64_t off = 0;
        while (off < keep) {
            uint64_t n = keep - off;
            if (n > VOPS_CHUNK) {
                n = VOPS_CHUNK;
            }
            int64_t r = vnode_read(vn, buf + off, n, off);
            if (r <= 0) {
                break;
            }
            off += (uint64_t)r;
        }
    }
    /* Recreate the file at the new size. */
    vfs_unlink(path);
    dcache_invalidate(path);
    if (vfs_create(path) != 0) {
        if (buf) kmem_free(buf);
        return -SYS_EIO;
    }
    struct vnode *nv = vfs_resolve(path);
    if (!nv) {
        if (buf) kmem_free(buf);
        return -SYS_EIO;
    }
    if (keep) {
        uint64_t off = 0;
        while (off < keep) {
            uint64_t n = keep - off;
            if (n > VOPS_CHUNK) {
                n = VOPS_CHUNK;
            }
            vnode_write(nv, buf + off, n, off);
            off += n;
        }
        kmem_free(buf);
    }
    /* Grow with zeros if extending. */
    if (length > old) {
        uint8_t zero[VOPS_CHUNK];
        memset(zero, 0, sizeof(zero));
        uint64_t off = old;
        while (off < length) {
            uint64_t n = length - off;
            if (n > VOPS_CHUNK) {
                n = VOPS_CHUNK;
            }
            vnode_write(nv, zero, n, off);
            off += n;
        }
    }
    return 0;
}
