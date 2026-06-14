/* VFS switch + fd-based file operations. */
#include "vfs.h"
#include "file.h"
#include "path.h"
#include "process.h"
#include "fd_table.h"
#include "syscall_numbers.h"
#include "string.h"
#include "log.h"

struct mount {
    char               path[VFS_PATH_MAX];
    struct filesystem *fs;
    int                used;
};

static struct mount g_mounts[VFS_MAX_MOUNTS];

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_mounts[i].used = 0;
    }
    kernel_log_ok("VFS online");
}

int vfs_mount(const char *path, struct filesystem *fs, void *data) {
    if (!path || !fs || path[0] != '/') {
        return -SYS_EINVAL;
    }
    if (data) {
        fs->data = data;
    }
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) {
            strlcpy(g_mounts[i].path, path, sizeof(g_mounts[i].path));
            g_mounts[i].fs = fs;
            g_mounts[i].used = 1;
            return 0;
        }
    }
    return -SYS_ENOMEM;
}

/* Longest-prefix mount match; *rel_out points at the path remainder (no leading
 * slash) within the chosen filesystem. */
static struct mount *find_mount(const char *abspath, const char **rel_out) {
    struct mount *best = NULL;
    uint64_t best_len = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_mounts[i].used) {
            continue;
        }
        const char *mp = g_mounts[i].path;
        uint64_t ml = strlen(mp);
        int match;
        if (ml == 1 && mp[0] == '/') {
            match = 1;   /* root mount matches everything */
        } else {
            match = (strncmp(abspath, mp, ml) == 0 &&
                     (abspath[ml] == '/' || abspath[ml] == 0));
        }
        if (match && ml >= best_len) {
            best = &g_mounts[i];
            best_len = ml;
        }
    }
    if (!best) {
        return NULL;
    }
    const char *rel = abspath + best_len;
    while (*rel == '/') {
        rel++;
    }
    *rel_out = rel;
    return best;
}

struct vnode *vfs_resolve(const char *abspath) {
    const char *rel = NULL;
    struct mount *m = find_mount(abspath, &rel);
    if (!m || !m->fs->lookup) {
        return NULL;
    }
    return m->fs->lookup(m->fs, rel);
}

/* ---- fd-based operations (current process) ------------------------------- */

static struct fd_table *current_fds(void) {
    struct process *p = process_current();
    return p ? process_fds(p) : NULL;
}

int vfs_open(const char *path, int flags) {
    struct process *p = process_current();
    if (!p) {
        return -SYS_EFAULT;
    }
    char abs[VFS_PATH_MAX];
    int r = path_resolve(process_cwd(p), path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    struct vnode *vn = vfs_resolve(abs);
    if (!vn) {
        return -SYS_ENOENT;
    }
    if ((flags & O_DIRECTORY) && vn->type != VNODE_DIR) {
        return -SYS_ENOTDIR;
    }
    struct file *f = file_alloc(vn, flags, abs);
    if (!f) {
        return -SYS_ENOMEM;
    }
    int fd = fd_alloc(process_fds(p), f);
    if (fd < 0) {
        file_unref(f);
        return fd;
    }
    return fd;
}

int vfs_close(int fd) {
    struct fd_table *t = current_fds();
    if (!t) {
        return -SYS_EBADF;
    }
    return fd_close(t, fd);
}

int64_t vfs_read(int fd, void *buffer, uint64_t size) {
    struct fd_table *t = current_fds();
    struct file *f = fd_get(t, fd);
    if (!f) {
        return -SYS_EBADF;
    }
    int64_t n = vnode_read(f->vnode, buffer, size, (uint64_t)f->offset);
    if (n > 0) {
        f->offset += n;
    }
    return n;
}

int64_t vfs_write(int fd, const void *buffer, uint64_t size) {
    struct fd_table *t = current_fds();
    struct file *f = fd_get(t, fd);
    if (!f) {
        return -SYS_EBADF;
    }
    int64_t n = vnode_write(f->vnode, buffer, size, (uint64_t)f->offset);
    if (n > 0) {
        f->offset += n;
    }
    return n;
}

int64_t vfs_lseek(int fd, int64_t offset, int whence) {
    struct fd_table *t = current_fds();
    struct file *f = fd_get(t, fd);
    if (!f) {
        return -SYS_EBADF;
    }
    int64_t base;
    switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = f->offset; break;
    case SEEK_END: base = (int64_t)f->vnode->size; break;
    default:       return -SYS_EINVAL;
    }
    int64_t no = base + offset;
    if (no < 0) {
        return -SYS_EINVAL;
    }
    f->offset = no;
    return no;
}

int vfs_readdir(int fd, struct dirent *out) {
    struct fd_table *t = current_fds();
    struct file *f = fd_get(t, fd);
    if (!f) {
        return -SYS_EBADF;
    }
    if (f->vnode->type != VNODE_DIR) {
        return -SYS_ENOTDIR;
    }
    int r = vnode_readdir(f->vnode, (uint64_t)f->offset, out);
    if (r == 0) {
        f->offset++;
        return 1;
    }
    return 0;   /* end of directory */
}

int vfs_stat(const char *path, struct stat *out) {
    struct process *p = process_current();
    char abs[VFS_PATH_MAX];
    int r = path_resolve(p ? process_cwd(p) : "/", path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    struct vnode *vn = vfs_resolve(abs);
    if (!vn) {
        return -SYS_ENOENT;
    }
    out->size = vn->size;
    out->type = (uint32_t)vn->type;
    out->mode = 0;
    return 0;
}
