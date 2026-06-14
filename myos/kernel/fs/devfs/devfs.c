/* devfs: maps the character-device registry into the VFS at /dev.
 *
 * The root is a single directory vnode; each device gets a lazily-bound chardev
 * vnode whose priv points at the struct char_device. */
#include "devfs.h"
#include "vfs.h"
#include "inode.h"
#include "device.h"
#include "char_device.h"
#include "string.h"
#include "log.h"

/* ---- chardev vnode ops --------------------------------------------------- */
static int64_t dev_read(struct vnode *vn, void *buf, uint64_t size, uint64_t offset) {
    (void)offset;
    return char_device_read((struct char_device *)vn->priv, buf, size);
}
static int64_t dev_write(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset) {
    (void)offset;
    return char_device_write((struct char_device *)vn->priv, buf, size);
}
static const struct vnode_ops dev_ops = { dev_read, dev_write, NULL };

/* ---- root directory vnode ops -------------------------------------------- */
static int dev_root_readdir(struct vnode *vn, uint64_t index, struct dirent *out) {
    (void)vn;
    struct char_device *cd = device_char_at((int)index);
    if (!cd) {
        return -1;
    }
    strlcpy(out->name, cd->name, sizeof(out->name));
    out->size = 0;
    out->type = VNODE_CHARDEV;
    return 0;
}
static const struct vnode_ops dev_root_ops = { NULL, NULL, dev_root_readdir };

static struct vnode g_root = { VNODE_DIR, 0, &dev_root_ops, NULL, NULL };

/* Lazily-bound per-device vnodes (one per registered char device). */
static struct vnode g_dev_vnodes[DEVICE_MAX_CHAR];
static struct char_device *g_bound[DEVICE_MAX_CHAR];
static int g_bound_count;

static struct vnode *bind_vnode(struct char_device *cd) {
    for (int i = 0; i < g_bound_count; i++) {
        if (g_bound[i] == cd) {
            return &g_dev_vnodes[i];
        }
    }
    if (g_bound_count >= DEVICE_MAX_CHAR) {
        return NULL;
    }
    int i = g_bound_count++;
    g_bound[i] = cd;
    g_dev_vnodes[i].type = VNODE_CHARDEV;
    g_dev_vnodes[i].size = 0;
    g_dev_vnodes[i].ops = &dev_ops;
    g_dev_vnodes[i].priv = cd;
    return &g_dev_vnodes[i];
}

static struct vnode *devfs_lookup(struct filesystem *fs, const char *rel) {
    (void)fs;
    if (rel[0] == 0) {
        return &g_root;
    }
    struct char_device *cd = device_find_char(rel);
    if (!cd) {
        return NULL;
    }
    return bind_vnode(cd);
}

static struct filesystem g_devfs = { "devfs", devfs_lookup, NULL };

struct filesystem *devfs_create(void) {
    g_bound_count = 0;
    g_root.fs = &g_devfs;
    kernel_log_ok("devfs online");
    return &g_devfs;
}
