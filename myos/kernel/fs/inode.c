/* vnode dispatch wrappers. */
#include "inode.h"
#include "syscall_numbers.h"

int64_t vnode_read(struct vnode *vn, void *buf, uint64_t size, uint64_t offset) {
    if (!vn || !vn->ops || !vn->ops->read) {
        return -SYS_EINVAL;
    }
    if (vn->type == VNODE_DIR) {
        return -SYS_EISDIR;
    }
    return vn->ops->read(vn, buf, size, offset);
}

int64_t vnode_write(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset) {
    if (!vn || !vn->ops || !vn->ops->write) {
        return -SYS_EINVAL;
    }
    if (vn->type == VNODE_DIR) {
        return -SYS_EISDIR;
    }
    return vn->ops->write(vn, buf, size, offset);
}

int vnode_readdir(struct vnode *vn, uint64_t index, struct dirent *out) {
    if (!vn || vn->type != VNODE_DIR || !vn->ops || !vn->ops->readdir) {
        return -SYS_ENOTDIR;
    }
    return vn->ops->readdir(vn, index, out);
}
