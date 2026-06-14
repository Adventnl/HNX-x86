/* HXE1 loading via the VFS. */
#include "exec.h"
#include "vfs.h"
#include "path.h"
#include "inode.h"
#include "user_loader.h"
#include "heap.h"
#include "string.h"
#include "syscall_numbers.h"

int exec_load(const char *cwd, const char *path,
              struct user_address_space *space, uint64_t *out_entry) {
    char abs[VFS_PATH_MAX];
    int r = path_resolve(cwd ? cwd : "/", path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    struct vnode *vn = vfs_resolve(abs);
    if (!vn) {
        return -SYS_ENOENT;
    }
    if (vn->type != VNODE_FILE) {
        return -SYS_EISDIR;
    }
    uint64_t size = vn->size;
    if (size < 32 /* sizeof(struct hxe_header) */) {
        return -SYS_ENOEXEC;
    }
    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf) {
        return -SYS_ENOMEM;
    }
    int64_t got = vnode_read(vn, buf, size, 0);
    if (got < 0 || (uint64_t)got != size) {
        kfree(buf);
        return -SYS_ENOEXEC;
    }
    int lr = user_loader_load(space, buf, size, out_entry);
    kfree(buf);
    return (lr == 0) ? 0 : -SYS_ENOEXEC;
}
