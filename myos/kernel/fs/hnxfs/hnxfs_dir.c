/* HNXFS directories: fixed 128-byte entries packed into the dir's data blocks. */
#include "hnxfs_dir.h"
#include "string.h"
#include "syscall_numbers.h"

static int name_eq(const char *a, const char *b) {
    return strncmp(a, b, HNXFS_NAME_MAX) == 0;
}

static int is_dot(const char *n) {
    return (n[0] == '.' && n[1] == 0) || (n[0] == '.' && n[1] == '.' && n[2] == 0);
}

uint64_t hnxfs_dir_lookup(uint64_t dir_inode, const char *name) {
    struct hnxfs_inode dir;
    if (hnxfs_read_inode(dir_inode, &dir) != 0 || dir.type != HNXFS_TYPE_DIR) {
        return 0;
    }
    uint8_t buf[HNXFS_BLOCK_SIZE];
    for (uint64_t b = 0; b < dir.blocks && b < HNXFS_DIRECT; b++) {
        if (hnxfs_read_block(dir.direct[b], buf) != 0) {
            continue;
        }
        for (uint32_t s = 0; s < HNXFS_DIRENTS_PER_BLOCK; s++) {
            struct hnxfs_dirent *de = (struct hnxfs_dirent *)(buf + s * 128);
            if (de->inode != 0 && name_eq(de->name, name)) {
                return de->inode;
            }
        }
    }
    return 0;
}

int hnxfs_dir_add(uint64_t dir_inode, const char *name, uint64_t child) {
    struct hnxfs_inode dir;
    if (hnxfs_read_inode(dir_inode, &dir) != 0 || dir.type != HNXFS_TYPE_DIR) {
        return -SYS_ENOTDIR;
    }
    uint8_t buf[HNXFS_BLOCK_SIZE];
    /* Existing blocks: look for a free slot. */
    for (uint64_t b = 0; b < dir.blocks && b < HNXFS_DIRECT; b++) {
        if (hnxfs_read_block(dir.direct[b], buf) != 0) {
            continue;
        }
        for (uint32_t s = 0; s < HNXFS_DIRENTS_PER_BLOCK; s++) {
            struct hnxfs_dirent *de = (struct hnxfs_dirent *)(buf + s * 128);
            if (de->inode == 0) {
                de->inode = child;
                strlcpy(de->name, name, HNXFS_NAME_MAX);
                return hnxfs_write_block(dir.direct[b], buf) == 0 ? 0 : -SYS_EIO;
            }
        }
    }
    /* Need a new directory data block. */
    if (dir.blocks >= HNXFS_DIRECT) {
        return -SYS_ENOMEM;
    }
    uint64_t blk = hnxfs_alloc_block();
    if (blk == 0) {
        return -SYS_ENOMEM;
    }
    dir.direct[dir.blocks] = blk;
    dir.blocks++;
    dir.size = dir.blocks * HNXFS_BLOCK_SIZE;
    if (hnxfs_write_inode(dir_inode, &dir) != 0) {
        return -SYS_EIO;
    }
    memset(buf, 0, sizeof(buf));
    struct hnxfs_dirent *de = (struct hnxfs_dirent *)buf;
    de->inode = child;
    strlcpy(de->name, name, HNXFS_NAME_MAX);
    return hnxfs_write_block(blk, buf) == 0 ? 0 : -SYS_EIO;
}

int hnxfs_dir_remove(uint64_t dir_inode, const char *name) {
    struct hnxfs_inode dir;
    if (hnxfs_read_inode(dir_inode, &dir) != 0 || dir.type != HNXFS_TYPE_DIR) {
        return -SYS_ENOTDIR;
    }
    uint8_t buf[HNXFS_BLOCK_SIZE];
    for (uint64_t b = 0; b < dir.blocks && b < HNXFS_DIRECT; b++) {
        if (hnxfs_read_block(dir.direct[b], buf) != 0) {
            continue;
        }
        for (uint32_t s = 0; s < HNXFS_DIRENTS_PER_BLOCK; s++) {
            struct hnxfs_dirent *de = (struct hnxfs_dirent *)(buf + s * 128);
            if (de->inode != 0 && name_eq(de->name, name)) {
                de->inode = 0;
                de->name[0] = 0;
                return hnxfs_write_block(dir.direct[b], buf) == 0 ? 0 : -SYS_EIO;
            }
        }
    }
    return -SYS_ENOENT;
}

int hnxfs_dir_get(uint64_t dir_inode, uint64_t index, struct hnxfs_dirent *out) {
    struct hnxfs_inode dir;
    if (hnxfs_read_inode(dir_inode, &dir) != 0 || dir.type != HNXFS_TYPE_DIR) {
        return -1;
    }
    uint8_t buf[HNXFS_BLOCK_SIZE];
    uint64_t seen = 0;
    for (uint64_t b = 0; b < dir.blocks && b < HNXFS_DIRECT; b++) {
        if (hnxfs_read_block(dir.direct[b], buf) != 0) {
            continue;
        }
        for (uint32_t s = 0; s < HNXFS_DIRENTS_PER_BLOCK; s++) {
            struct hnxfs_dirent *de = (struct hnxfs_dirent *)(buf + s * 128);
            if (de->inode == 0 || is_dot(de->name)) {
                continue;
            }
            if (seen == index) {
                *out = *de;
                return 0;
            }
            seen++;
        }
    }
    return -1;
}

int hnxfs_dir_empty(uint64_t dir_inode) {
    struct hnxfs_dirent de;
    return hnxfs_dir_get(dir_inode, 0, &de) != 0;
}
