/* HNXFS mount + VFS glue (block I/O, vnode cache, vnode ops, path lookup). */
#include "hnxfs.h"
#include "hnxfs_dir.h"
#include "hnxfs_file.h"
#include "block_device.h"
#include "block_registry.h"
#include "string.h"
#include "log.h"
#include "syscall_numbers.h"

struct hnxfs g_hnxfs;

int hnxfs_read_block(uint64_t fsblk, void *buf) {
    return block_read(g_hnxfs.dev, fsblk * HNXFS_SECTORS_PER_BLOCK, buf,
                      HNXFS_SECTORS_PER_BLOCK);
}

int hnxfs_write_block(uint64_t fsblk, const void *buf) {
    return block_write(g_hnxfs.dev, fsblk * HNXFS_SECTORS_PER_BLOCK, buf,
                       HNXFS_SECTORS_PER_BLOCK);
}

static enum vnode_type type_of(uint32_t hnxfs_type) {
    return (hnxfs_type == HNXFS_TYPE_DIR) ? VNODE_DIR : VNODE_FILE;
}

/* ---- vnode ops ----------------------------------------------------------- */
static int64_t v_read(struct vnode *vn, void *buf, uint64_t size, uint64_t offset) {
    return hnxfs_file_read((uint64_t)(uintptr_t)vn->priv, buf, size, offset);
}

static int64_t v_write(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset) {
    int64_t w = hnxfs_file_write((uint64_t)(uintptr_t)vn->priv, buf, size, offset);
    struct hnxfs_inode in;
    if (hnxfs_read_inode((uint64_t)(uintptr_t)vn->priv, &in) == 0) {
        vn->size = in.size;
    }
    return w;
}

static int v_readdir(struct vnode *vn, uint64_t index, struct dirent *out) {
    struct hnxfs_dirent de;
    if (hnxfs_dir_get((uint64_t)(uintptr_t)vn->priv, index, &de) != 0) {
        return -1;
    }
    strlcpy(out->name, de.name, sizeof(out->name));
    struct hnxfs_inode in;
    out->size = 0;
    out->type = VNODE_FILE;
    if (hnxfs_read_inode(de.inode, &in) == 0) {
        out->size = in.size;
        out->type = type_of(in.type);
    }
    return 0;
}

static int v_create(struct vnode *dir, const char *name, enum vnode_type type,
                    struct vnode **outp) {
    uint64_t dir_inode = (uint64_t)(uintptr_t)dir->priv;
    if (hnxfs_dir_lookup(dir_inode, name) != 0) {
        return -SYS_EEXIST;
    }
    uint64_t child = hnxfs_alloc_inode();
    if (child == 0) {
        return -SYS_ENOMEM;
    }
    struct hnxfs_inode in;
    memset(&in, 0, sizeof(in));
    in.type = (type == VNODE_DIR) ? HNXFS_TYPE_DIR : HNXFS_TYPE_FILE;
    in.mode = 0644;
    if (hnxfs_write_inode(child, &in) != 0) {
        hnxfs_free_inode(child);
        return -SYS_EIO;
    }
    if (hnxfs_dir_add(dir_inode, name, child) != 0) {
        hnxfs_free_inode(child);
        return -SYS_EIO;
    }
    if (outp) {
        *outp = hnxfs_get_vnode(child);
    }
    return 0;
}

static int v_unlink(struct vnode *dir, const char *name) {
    uint64_t dir_inode = (uint64_t)(uintptr_t)dir->priv;
    uint64_t child = hnxfs_dir_lookup(dir_inode, name);
    if (child == 0) {
        return -SYS_ENOENT;
    }
    struct hnxfs_inode in;
    if (hnxfs_read_inode(child, &in) != 0) {
        return -SYS_EIO;
    }
    if (in.type == HNXFS_TYPE_DIR && !hnxfs_dir_empty(child)) {
        return -SYS_EINVAL;       /* refuse to remove a non-empty directory */
    }
    hnxfs_file_truncate(child);
    hnxfs_free_inode(child);
    return hnxfs_dir_remove(dir_inode, name);
}

static const struct vnode_ops g_hnxfs_ops = {
    v_read, v_write, v_readdir, v_create, v_unlink,
};

/* ---- vnode cache --------------------------------------------------------- */
struct vnode *hnxfs_get_vnode(uint64_t inode_num) {
    for (int i = 0; i < g_hnxfs.vnode_count; i++) {
        if (g_hnxfs.vnode_inode[i] == inode_num) {
            struct hnxfs_inode in;
            if (hnxfs_read_inode(inode_num, &in) == 0) {
                g_hnxfs.vnodes[i].type = type_of(in.type);
                g_hnxfs.vnodes[i].size = in.size;
            }
            return &g_hnxfs.vnodes[i];
        }
    }
    int slot;
    if (g_hnxfs.vnode_count < HNXFS_VNODE_CACHE) {
        slot = g_hnxfs.vnode_count++;
    } else {
        slot = (int)(inode_num % HNXFS_VNODE_CACHE);   /* reuse (rare) */
    }
    struct hnxfs_inode in;
    if (hnxfs_read_inode(inode_num, &in) != 0) {
        return NULL;
    }
    g_hnxfs.vnode_inode[slot] = inode_num;
    struct vnode *vn = &g_hnxfs.vnodes[slot];
    vn->type = type_of(in.type);
    vn->size = in.size;
    vn->ops = &g_hnxfs_ops;
    vn->fs = &g_hnxfs.fs;
    vn->priv = (void *)(uintptr_t)inode_num;
    return vn;
}

/* ---- path lookup --------------------------------------------------------- */
static struct vnode *hnxfs_lookup(struct filesystem *fs, const char *rel) {
    (void)fs;
    uint64_t inode = HNXFS_ROOT_INODE;
    const char *p = rel;
    char comp[HNXFS_NAME_MAX];
    while (*p) {
        while (*p == '/') {
            p++;
        }
        uint64_t n = 0;
        while (*p && *p != '/' && n < HNXFS_NAME_MAX - 1) {
            comp[n++] = *p++;
        }
        if (n == 0) {
            break;
        }
        comp[n] = 0;
        inode = hnxfs_dir_lookup(inode, comp);
        if (inode == 0) {
            return NULL;
        }
    }
    return hnxfs_get_vnode(inode);
}

struct filesystem *hnxfs_mount(struct block_device *dev) {
    if (!dev) {
        return NULL;
    }
    uint8_t buf[HNXFS_BLOCK_SIZE];
    if (block_read(dev, 0, buf, HNXFS_SECTORS_PER_BLOCK) != 0) {
        return NULL;
    }
    struct hnxfs_superblock *sb = (struct hnxfs_superblock *)buf;
    if (sb->magic != HNXFS_MAGIC || sb->version != HNXFS_VERSION) {
        kernel_log_error("hnxfs: bad superblock magic/version");
        return NULL;
    }
    g_hnxfs.dev = dev;
    g_hnxfs.sb = *sb;
    g_hnxfs.vnode_count = 0;
    g_hnxfs.mounted = 1;
    g_hnxfs.fs.name = "hnxfs";
    g_hnxfs.fs.lookup = hnxfs_lookup;
    g_hnxfs.fs.data = &g_hnxfs;
    return &g_hnxfs.fs;
}
