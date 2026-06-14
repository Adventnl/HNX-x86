/* ramfs: read-only tree over the initramfs blobs. */
#include "ramfs.h"
#include "vfs.h"
#include "inode.h"
#include "initramfs.h"
#include "heap.h"
#include "string.h"
#include "syscall_numbers.h"

struct ramfs_node {
    char                name[VFS_NAME_MAX];
    enum vnode_type     type;
    const void         *data;          /* file blob (NULL for dirs) */
    uint64_t            size;
    struct ramfs_node  *first_child;
    struct ramfs_node  *next_sibling;
    struct vnode        vnode;         /* embedded; .priv -> this node */
};

static struct filesystem g_ramfs;
static struct ramfs_node *g_root;

/* ---- vnode ops ----------------------------------------------------------- */
static int64_t ramfs_read(struct vnode *vn, void *buf, uint64_t size, uint64_t offset) {
    struct ramfs_node *n = (struct ramfs_node *)vn->priv;
    if (offset >= n->size) {
        return 0;
    }
    uint64_t avail = n->size - offset;
    uint64_t cnt = (size < avail) ? size : avail;
    memcpy(buf, (const uint8_t *)n->data + offset, cnt);
    return (int64_t)cnt;
}

static int64_t ramfs_write(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset) {
    (void)vn; (void)buf; (void)size; (void)offset;
    return -SYS_EPERM;   /* read-only */
}

static int ramfs_readdir(struct vnode *vn, uint64_t index, struct dirent *out) {
    struct ramfs_node *dir = (struct ramfs_node *)vn->priv;
    struct ramfs_node *c = dir->first_child;
    for (uint64_t i = 0; c && i < index; i++) {
        c = c->next_sibling;
    }
    if (!c) {
        return -1;
    }
    strlcpy(out->name, c->name, sizeof(out->name));
    out->size = c->size;
    out->type = (uint32_t)c->type;
    return 0;
}

static const struct vnode_ops ramfs_file_ops = { ramfs_read, ramfs_write, NULL, NULL, NULL };
static const struct vnode_ops ramfs_dir_ops  = { NULL, NULL, ramfs_readdir, NULL, NULL };

/* ---- tree construction --------------------------------------------------- */
static struct ramfs_node *node_new(const char *name, uint64_t name_len,
                                   enum vnode_type type, const void *data, uint64_t size) {
    struct ramfs_node *n = (struct ramfs_node *)kcalloc(1, sizeof(*n));
    if (!n) {
        return NULL;
    }
    if (name_len >= sizeof(n->name)) {
        name_len = sizeof(n->name) - 1;
    }
    memcpy(n->name, name, name_len);
    n->name[name_len] = 0;
    n->type = type;
    n->data = data;
    n->size = size;
    n->vnode.type = type;
    n->vnode.size = size;
    n->vnode.ops = (type == VNODE_DIR) ? &ramfs_dir_ops : &ramfs_file_ops;
    n->vnode.fs = &g_ramfs;
    n->vnode.priv = n;
    return n;
}

static struct ramfs_node *find_child(struct ramfs_node *dir, const char *name, uint64_t len) {
    for (struct ramfs_node *c = dir->first_child; c; c = c->next_sibling) {
        if (strlen(c->name) == len && strncmp(c->name, name, len) == 0) {
            return c;
        }
    }
    return NULL;
}

static void link_child(struct ramfs_node *dir, struct ramfs_node *child) {
    child->next_sibling = dir->first_child;
    dir->first_child = child;
}

static struct ramfs_node *ensure_dir(struct ramfs_node *parent, const char *name, uint64_t len) {
    struct ramfs_node *c = find_child(parent, name, len);
    if (c) {
        return c;
    }
    c = node_new(name, len, VNODE_DIR, NULL, 0);
    if (c) {
        link_child(parent, c);
    }
    return c;
}

/* Insert "/a/b/file" into the tree, creating intermediate dirs. */
static void insert_path(const char *path, const void *data, uint64_t size) {
    struct ramfs_node *cur = g_root;
    const char *p = path;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        uint64_t len = (uint64_t)(p - start);
        if (len == 0) {
            break;
        }
        int is_last = 1;
        for (const char *q = p; *q; q++) {
            if (*q != '/') { is_last = 0; break; }
        }
        if (is_last) {
            if (!find_child(cur, start, len)) {
                struct ramfs_node *f = node_new(start, len, VNODE_FILE, data, size);
                if (f) {
                    link_child(cur, f);
                }
            }
            return;
        }
        cur = ensure_dir(cur, start, len);
        if (!cur) {
            return;
        }
    }
}

/* ---- lookup -------------------------------------------------------------- */
static struct vnode *ramfs_lookup(struct filesystem *fs, const char *rel) {
    (void)fs;
    struct ramfs_node *cur = g_root;
    const char *p = rel;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        const char *start = p;
        while (*p && *p != '/') {
            p++;
        }
        uint64_t len = (uint64_t)(p - start);
        if (len == 0) {
            break;
        }
        cur = find_child(cur, start, len);
        if (!cur) {
            return NULL;
        }
    }
    return &cur->vnode;
}

struct filesystem *ramfs_create_from_initramfs(void) {
    if (!initramfs_is_available()) {
        return NULL;
    }
    g_ramfs.name = "ramfs";
    g_ramfs.lookup = ramfs_lookup;
    g_ramfs.data = NULL;

    g_root = node_new("", 0, VNODE_DIR, NULL, 0);
    if (!g_root) {
        return NULL;
    }

    uint32_t n = initramfs_count();
    for (uint32_t i = 0; i < n; i++) {
        const char *path = initramfs_path_at(i);
        uint64_t size = 0;
        const void *data = initramfs_data_at(i, &size);
        if (path && path[0] == '/') {
            insert_path(path, data, size);
        }
    }
    return &g_ramfs;
}
