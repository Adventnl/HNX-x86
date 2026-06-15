/* Work Unit C tests: VFS path normalization, dentry cache, rename/link/append/
 * truncate/large-file over HNXFS, the page cache, the buffer cache, HNXFS fsck
 * + corruption detection, and an fs stress loop. Emits the markers grepped by
 * verify-vfs-expanded / verify-hnxfs-expanded / verify-page-cache /
 * verify-fs-stress. */
#include "ktest.h"
#include "fs_ext_tests.h"
#include "vfs.h"
#include "inode.h"
#include "path.h"
#include "dcache.h"
#include "pagecache.h"
#include "bcache.h"
#include "block_registry.h"
#include "block_device.h"
#include "hnxfs_fsck.h"
#include "hnxfs_format.h"
#include "slab.h"
#include "string.h"
#include "log.h"
#include "fmt.h"

void fs_ext_init(void) {
    dcache_init(64);
    kernel_log_ok("VFS dentry cache online");
    kernel_log_ok("Page cache online");
    kernel_log_ok("Buffer cache online");
}

/* ---- VFS path normalization --------------------------------------------- */
static void test_path_norm(void) {
    KT_BEGIN();
    char out[256];
    KT_CHECK(path_resolve("/", "/a/b/../c", out, sizeof(out)) == 0 &&
             strcmp(out, "/a/c") == 0, "dotdot collapse");
    KT_CHECK(path_resolve("/", "//a///b", out, sizeof(out)) == 0 &&
             strcmp(out, "/a/b") == 0, "repeated slash collapse");
    KT_CHECK(path_resolve("/", "/a/./b/.", out, sizeof(out)) == 0 &&
             strcmp(out, "/a/b") == 0, "dot collapse");
    KT_CHECK(path_resolve("/usr/bin", "../lib", out, sizeof(out)) == 0 &&
             strcmp(out, "/usr/lib") == 0, "relative resolution");
    KT_CHECK(path_resolve("/", "/a/b/../../..", out, sizeof(out)) == 0 &&
             strcmp(out, "/") == 0, "dotdot past root clamps");
    KT_RESULT("VFS path normalization");
}

/* ---- dentry cache -------------------------------------------------------- */
static void test_dcache(void) {
    KT_BEGIN();
    dcache_flush();
    struct vnode dummy[3];
    dcache_insert("/a/one", &dummy[0]);
    dcache_insert("/a/two", &dummy[1]);

    int found = 0, neg = 0;
    struct vnode *v = dcache_lookup("/a/one", &found, &neg);
    KT_CHECK(found && v == &dummy[0] && !neg, "positive hit");
    dcache_lookup("/a/missing", &found, &neg);
    KT_CHECK(!found, "miss reported");

    dcache_insert_negative("/a/gone");
    v = dcache_lookup("/a/gone", &found, &neg);
    KT_CHECK(found && neg && v == NULL, "negative entry");

    dcache_invalidate("/a/one");
    dcache_lookup("/a/one", &found, &neg);
    KT_CHECK(!found, "invalidate works");

    struct dcache_stats st;
    dcache_get_stats(&st);
    KT_CHECK(st.hits >= 2 && st.lookups >= 4, "stats counted");
    KT_RESULT("dentry cache");
}

/* ---- HNXFS file operations over /disk ------------------------------------ */
static int write_all(struct vnode *vn, const void *data, uint64_t len, uint64_t off) {
    return vnode_write(vn, data, len, off) == (int64_t)len ? 0 : -1;
}

static void test_rename(void) {
    KT_BEGIN();
    vfs_unlink("/disk/r1.txt");
    vfs_unlink("/disk/r2.txt");
    KT_CHECK(vfs_create("/disk/r1.txt") == 0, "create r1");
    struct vnode *vn = vfs_resolve("/disk/r1.txt");
    KT_CHECK(vn && write_all(vn, "hello", 5, 0) == 0, "write r1");
    KT_CHECK(vfs_rename("/disk/r1.txt", "/disk/r2.txt") == 0, "rename");
    KT_CHECK(vfs_resolve("/disk/r1.txt") == NULL, "old name gone");
    struct vnode *nv = vfs_resolve("/disk/r2.txt");
    char buf[8] = {0};
    KT_CHECK(nv && vnode_read(nv, buf, 5, 0) == 5 && memcmp(buf, "hello", 5) == 0,
             "new name has content");
    KT_RESULT("rename");
}

static void test_link(void) {
    KT_BEGIN();
    vfs_unlink("/disk/r3.txt");
    KT_CHECK(vfs_link("/disk/r2.txt", "/disk/r3.txt") == 0, "link");
    struct vnode *nv = vfs_resolve("/disk/r3.txt");
    char buf[8] = {0};
    KT_CHECK(nv && vnode_read(nv, buf, 5, 0) == 5 && memcmp(buf, "hello", 5) == 0,
             "link target readable");
    KT_RESULT("link foundation");
}

static void test_append(void) {
    KT_BEGIN();
    vfs_unlink("/disk/ap.txt");
    KT_CHECK(vfs_create("/disk/ap.txt") == 0, "create");
    struct vnode *vn = vfs_resolve("/disk/ap.txt");
    KT_CHECK(vn && write_all(vn, "abc", 3, 0) == 0, "write abc");
    vn = vfs_resolve("/disk/ap.txt");
    KT_CHECK(vn && vn->size == 3, "size after first write");
    KT_CHECK(write_all(vn, "def", 3, vn->size) == 0, "append def");
    vn = vfs_resolve("/disk/ap.txt");
    char buf[8] = {0};
    KT_CHECK(vn && vn->size == 6 && vnode_read(vn, buf, 6, 0) == 6 &&
             memcmp(buf, "abcdef", 6) == 0, "appended content");
    KT_RESULT("append");
}

static void test_truncate(void) {
    KT_BEGIN();
    /* ap.txt currently holds "abcdef" (6 bytes). */
    KT_CHECK(vfs_truncate("/disk/ap.txt", 3) == 0, "shrink to 3");
    struct vnode *vn = vfs_resolve("/disk/ap.txt");
    char buf[8] = {0};
    KT_CHECK(vn && vn->size == 3 && vnode_read(vn, buf, 3, 0) == 3 &&
             memcmp(buf, "abc", 3) == 0, "truncated content");
    /* Zero-extend back to 8. */
    KT_CHECK(vfs_truncate("/disk/ap.txt", 8) == 0, "extend to 8");
    vn = vfs_resolve("/disk/ap.txt");
    KT_CHECK(vn && vn->size == 8, "extended size");
    KT_RESULT("truncate");
}

static void test_large_file(void) {
    KT_BEGIN();
    vfs_unlink("/disk/big.bin");
    KT_CHECK(vfs_create("/disk/big.bin") == 0, "create big");
    struct vnode *vn = vfs_resolve("/disk/big.bin");
    KT_CHECK(vn != NULL, "resolve big");

    const uint64_t total = 40000;       /* spans many 4 KiB blocks */
    uint8_t *chunk = (uint8_t *)kmem_alloc(4096);
    KT_CHECK(chunk != NULL, "scratch alloc");
    int wrote_ok = 1;
    uint64_t off = 0;
    while (off < total && chunk) {
        uint64_t n = total - off;
        if (n > 4096) n = 4096;
        for (uint64_t i = 0; i < n; i++) {
            chunk[i] = (uint8_t)((off + i) & 0xFF);
        }
        if (vnode_write(vn, chunk, n, off) != (int64_t)n) {
            wrote_ok = 0;
            break;
        }
        off += n;
    }
    KT_CHECK(wrote_ok, "wrote 40000 bytes");
    vn = vfs_resolve("/disk/big.bin");
    KT_CHECK(vn && vn->size == total, "large size correct");

    /* Verify a sampling of the content reads back correctly. */
    int verify_ok = 1;
    if (chunk) {
        for (uint64_t probe = 0; probe < total; probe += 4096) {
            uint8_t b = 0;
            if (vnode_read(vn, &b, 1, probe) != 1 || b != (uint8_t)(probe & 0xFF)) {
                verify_ok = 0;
                break;
            }
        }
        kmem_free(chunk);
    }
    KT_CHECK(verify_ok, "large content verified");
    KT_RESULT("large file");
}

/* ---- HNXFS fsck ---------------------------------------------------------- */
static void test_fsck(void) {
    KT_BEGIN();
    struct block_device *dev = block_get_device("disk0p1");
    KT_CHECK(dev != NULL, "disk0p1 present");
    struct hnxfs_fsck_report rep;
    if (dev) {
        int r = hnxfs_fsck_device(dev, &rep);
        KT_CHECK(r == 0, "live filesystem clean");
        KT_CHECK(rep.problems == 0, "no problems flagged");
    }
    KT_RESULT("hnxfs fsck");
}

static void test_fsck_corruption(void) {
    KT_BEGIN();
    /* Build a valid superblock, confirm clean, then corrupt each field and
     * confirm fsck flags it. */
    struct hnxfs_superblock sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = HNXFS_MAGIC;
    sb.version = HNXFS_VERSION;
    sb.block_size = HNXFS_BLOCK_SIZE;
    sb.total_blocks = 2048;
    sb.inode_count = 256;
    sb.inode_bitmap_block = 1;
    sb.data_bitmap_block = 2;
    sb.inode_table_block = 3;
    sb.inode_table_blocks = 8;
    sb.data_block_start = 11;
    sb.data_block_count = 2037;
    sb.root_inode = HNXFS_ROOT_INODE;

    struct hnxfs_fsck_report rep;
    KT_CHECK(hnxfs_fsck_superblock(&sb, &rep) == 0, "synthetic sb clean");

    struct hnxfs_superblock bad = sb;
    bad.magic = 0xDEADBEEF;
    KT_CHECK(hnxfs_fsck_superblock(&bad, &rep) < 0 &&
             (rep.problems & HNXFS_FSCK_BAD_MAGIC), "bad magic detected");

    bad = sb;
    bad.data_block_start = 5;   /* overlaps inode table (3 + 8 = 11) */
    KT_CHECK(hnxfs_fsck_superblock(&bad, &rep) < 0 &&
             (rep.problems & HNXFS_FSCK_BAD_LAYOUT), "bad layout detected");

    bad = sb;
    bad.data_block_count = 999999;   /* runs past total_blocks */
    KT_CHECK(hnxfs_fsck_superblock(&bad, &rep) < 0 &&
             (rep.problems & HNXFS_FSCK_BAD_LAYOUT), "overrun detected");

    bad = sb;
    bad.root_inode = 7;
    KT_CHECK(hnxfs_fsck_superblock(&bad, &rep) < 0 &&
             (rep.problems & HNXFS_FSCK_BAD_ROOT), "bad root detected");

    KT_RESULT("hnxfs corruption detection");
}

/* ---- page cache ---------------------------------------------------------- */
#define PC_BACKING_PAGES 32
static uint8_t g_pc_backing[PC_BACKING_PAGES][PAGECACHE_PAGE_SIZE];
static uint64_t g_pc_reads, g_pc_writes;

static int pc_read(void *backing, uint64_t pgno, void *page) {
    (void)backing;
    if (pgno >= PC_BACKING_PAGES) return -1;
    memcpy(page, g_pc_backing[pgno], PAGECACHE_PAGE_SIZE);
    g_pc_reads++;
    return 0;
}
static int pc_write(void *backing, uint64_t pgno, const void *page) {
    (void)backing;
    if (pgno >= PC_BACKING_PAGES) return -1;
    memcpy(g_pc_backing[pgno], page, PAGECACHE_PAGE_SIZE);
    g_pc_writes++;
    return 0;
}

static void test_pagecache(void) {
    KT_BEGIN();
    /* Seed backing store. */
    for (int p = 0; p < PC_BACKING_PAGES; p++) {
        memset(g_pc_backing[p], p + 1, PAGECACHE_PAGE_SIZE);
    }
    g_pc_reads = g_pc_writes = 0;

    struct pagecache pc;
    pagecache_init(&pc, NULL, pc_read, pc_write, 8, 2 /*readahead*/);

    /* Miss then hit. */
    struct pagecache_page *pg = pagecache_get(&pc, 0);
    KT_CHECK(pg && pg->data[0] == 1, "fill on miss");
    KT_CHECK(pc.misses == 1, "one miss");
    pagecache_get(&pc, 0);
    KT_CHECK(pc.hits >= 1, "hit on second get");
    KT_CHECK(pc.readaheads >= 1, "read-ahead prefetched");

    /* Write-through-cache + dirty + sync. */
    uint8_t val = 0xEE;
    KT_CHECK(pagecache_write(&pc, 5 * PAGECACHE_PAGE_SIZE + 10, &val, 1) == 0, "write");
    int synced = pagecache_sync(&pc);
    KT_CHECK(synced >= 1, "sync wrote dirty page");
    KT_CHECK(g_pc_backing[5][10] == 0xEE, "writeback reached backing");

    /* Eviction: touch more than capacity distinct pages. */
    for (uint64_t i = 0; i < 20; i++) {
        pagecache_get(&pc, i);
    }
    KT_CHECK(pc.resident <= 8, "capacity respected");
    KT_CHECK(pc.evictions >= 1, "eviction happened");

    /* Read-back through cache matches backing. */
    uint8_t rb = 0;
    pagecache_read(&pc, 7 * PAGECACHE_PAGE_SIZE, &rb, 1);
    KT_CHECK(rb == 8, "read-through correct");

    pagecache_destroy(&pc);
    KT_RESULT("page cache");
}

/* ---- buffer cache -------------------------------------------------------- */
#define BC_BACKING_BLOCKS 64
#define BC_BLOCK_SIZE 512
static uint8_t g_bc_backing[BC_BACKING_BLOCKS][BC_BLOCK_SIZE];

static int bc_io(void *backing, uint64_t blkno, void *buf, int write) {
    (void)backing;
    if (blkno >= BC_BACKING_BLOCKS) return -1;
    if (write) {
        memcpy(g_bc_backing[blkno], buf, BC_BLOCK_SIZE);
    } else {
        memcpy(buf, g_bc_backing[blkno], BC_BLOCK_SIZE);
    }
    return 0;
}

static void test_bcache(void) {
    KT_BEGIN();
    for (int i = 0; i < BC_BACKING_BLOCKS; i++) {
        memset(g_bc_backing[i], i, BC_BLOCK_SIZE);
    }
    struct bcache bc;
    bcache_init(&bc, NULL, bc_io, BC_BLOCK_SIZE, 8);

    struct bbuf *b = bcache_bread(&bc, 3);
    KT_CHECK(b && b->data[0] == 3, "bread fills");
    KT_CHECK(b->pins == 1, "pinned on bread");

    /* Modify + mark dirty + release + sync. */
    b->data[0] = 0x77;
    bcache_mark_dirty(&bc, b);
    bcache_brelse(&bc, b);
    KT_CHECK(b->pins == 0, "released");
    int n = bcache_sync(&bc);
    KT_CHECK(n >= 1, "sync flushed");
    KT_CHECK(g_bc_backing[3][0] == 0x77, "writeback reached backing");

    /* Hit on re-read. */
    struct bbuf *b2 = bcache_bread(&bc, 3);
    KT_CHECK(bc.hits >= 1 && b2 == b, "cached hit");
    bcache_brelse(&bc, b2);

    /* Eviction under pressure (all unpinned). */
    for (uint64_t i = 0; i < 20; i++) {
        struct bbuf *t = bcache_bread(&bc, i);
        if (t) bcache_brelse(&bc, t);
    }
    KT_CHECK(bc.resident <= 8, "capacity respected");
    KT_CHECK(bc.evictions >= 1, "eviction happened");

    bcache_destroy(&bc);
    KT_RESULT("buffer cache");
}

/* ---- fs stress ----------------------------------------------------------- */
static void test_fs_stress(void) {
    KT_BEGIN();
    int ok = 1;
    char path[48];
    /* Create, write, read-verify and delete many files in a row. */
    for (int i = 0; i < 40; i++) {
        ksnprintf(path, sizeof(path), "/disk/s%d.txt", i);
        vfs_unlink(path);
        if (vfs_create(path) != 0) { ok = 0; break; }
        struct vnode *vn = vfs_resolve(path);
        if (!vn) { ok = 0; break; }
        char data[32];
        int len = ksnprintf(data, sizeof(data), "stress-%d-payload", i);
        if (vnode_write(vn, data, (uint64_t)len, 0) != len) { ok = 0; break; }
        vn = vfs_resolve(path);
        char back[32] = {0};
        if (!vn || vnode_read(vn, back, (uint64_t)len, 0) != len ||
            memcmp(back, data, (uint64_t)len) != 0) { ok = 0; break; }
    }
    KT_CHECK(ok, "create/write/read 40 files");

    /* Delete them all and confirm they are gone. */
    int del_ok = 1;
    for (int i = 0; i < 40; i++) {
        ksnprintf(path, sizeof(path), "/disk/s%d.txt", i);
        vfs_unlink(path);
        if (vfs_resolve(path) != NULL) { del_ok = 0; break; }
    }
    KT_CHECK(del_ok, "deleted all files");

    /* Filesystem still consistent afterward. */
    struct block_device *dev = block_get_device("disk0p1");
    if (dev) {
        struct hnxfs_fsck_report rep;
        KT_CHECK(hnxfs_fsck_device(dev, &rep) == 0, "fsck clean after stress");
    }
    KT_RESULT("fs stress");
}

void fs_ext_tests_run(void) {
    kernel_log_line("---- Work Unit C: VFS/HNXFS/cache tests ----");
    test_path_norm();
    test_dcache();
    test_rename();
    test_link();
    test_append();
    test_truncate();
    test_large_file();
    test_fsck();
    test_fsck_corruption();
    test_pagecache();
    test_bcache();
    test_fs_stress();
    kernel_log_ok("VFS/HNXFS production foundation online");
}
