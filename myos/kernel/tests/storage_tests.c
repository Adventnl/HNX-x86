/* Kernel-side storage tests: block cache, partitions, AHCI disk read/write,
 * and HNXFS file/dir operations. Run with the kernel CR3 active. */
#include "storage_tests.h"
#include "block_registry.h"
#include "block_device.h"
#include "block_cache.h"
#include "vfs.h"
#include "inode.h"
#include "string.h"
#include "log.h"

void storage_tests_run(void) {
    struct block_device *disk = block_get_device("disk0");
    if (!disk) {
        kernel_log_error("storage: disk0 not present");
        return;
    }

    /* --- block cache: a re-read of the same LBA must hit. --- */
    uint8_t b0[BLOCK_SECTOR_SIZE], b1[BLOCK_SECTOR_SIZE];
    uint64_t hits_before = block_cache_hits();
    block_read(disk, 0, b0, 1);                 /* miss + populate */
    block_read(disk, 0, b1, 1);                 /* hit */
    if (block_cache_hits() > hits_before) {
        kernel_log_line("[PASS] block cache");
    } else {
        kernel_log_error("block cache: no hit on re-read");
    }

    /* --- disk read: the MBR signature proves a real sector came back. --- */
    if (b0[510] == 0x55 && b0[511] == 0xAA) {
        kernel_log_line("[PASS] disk read");
    } else {
        kernel_log_error("disk read: bad MBR signature");
    }

    /* --- partition parser: disk0p1 must have been registered. --- */
    if (block_get_device("disk0p1")) {
        kernel_log_line("[PASS] partition parser");
    } else {
        kernel_log_error("partition parser: disk0p1 missing");
    }

    /* --- disk write: round-trip through the scratch partition's raw ops
     *     (bypassing the cache so the data really hits the platter). --- */
    struct block_device *scratch = block_get_device("disk0p2");
    if (scratch && scratch->read && scratch->write) {
        uint8_t w[BLOCK_SECTOR_SIZE], r[BLOCK_SECTOR_SIZE];
        for (uint32_t i = 0; i < BLOCK_SECTOR_SIZE; i++) {
            w[i] = (uint8_t)(i ^ 0x5A);
        }
        memset(r, 0, sizeof(r));
        if (scratch->write(scratch, 0, w, 1) == 0 &&
            scratch->read(scratch, 0, r, 1) == 0 &&
            memcmp(w, r, BLOCK_SECTOR_SIZE) == 0) {
            kernel_log_line("[PASS] disk write");
        } else {
            kernel_log_error("disk write: round-trip mismatch");
        }
    } else {
        kernel_log_error("disk write: disk0p2 scratch missing");
    }
}

void hnxfs_tests_run(void) {
    /* create file */
    if (vfs_create("/disk/hello.txt") == 0) {
        kernel_log_line("[PASS] hnxfs create file");
    } else {
        kernel_log_error("hnxfs create file failed");
        return;
    }

    /* write file */
    struct vnode *vn = vfs_resolve("/disk/hello.txt");
    const char *msg = "HNXFS persistence works!\n";
    uint64_t mlen = strlen(msg);
    if (vn && vnode_write(vn, msg, mlen, 0) == (int64_t)mlen) {
        kernel_log_line("[PASS] hnxfs write file");
    } else {
        kernel_log_error("hnxfs write file failed");
    }

    /* read file */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    vn = vfs_resolve("/disk/hello.txt");
    if (vn && vnode_read(vn, buf, mlen, 0) == (int64_t)mlen &&
        memcmp(buf, msg, mlen) == 0) {
        kernel_log_line("[PASS] hnxfs read file");
    } else {
        kernel_log_error("hnxfs read file failed");
    }

    /* mkdir */
    if (vfs_mkdir("/disk/subdir") == 0 && vfs_resolve("/disk/subdir")) {
        kernel_log_line("[PASS] hnxfs mkdir");
    } else {
        kernel_log_error("hnxfs mkdir failed");
    }

    /* unlink */
    if (vfs_unlink("/disk/hello.txt") == 0 && vfs_resolve("/disk/hello.txt") == NULL) {
        kernel_log_line("[PASS] hnxfs unlink");
    } else {
        kernel_log_error("hnxfs unlink failed");
    }
}
