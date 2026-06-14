/* Kernel-side VFS checks (run with the kernel CR3 active, so initramfs reads
 * are valid). Exercises resolve, chardev typing, file read and root readdir. */
#include "vfs_tests.h"
#include "vfs.h"
#include "inode.h"
#include "string.h"
#include "log.h"

void vfs_tests_run(void) {
    int ok = 1;

    struct vnode *banner = vfs_resolve("/etc/banner.txt");
    if (!banner || banner->type != VNODE_FILE || banner->size == 0) {
        ok = 0;
    } else {
        char buf[16];
        if (vnode_read(banner, buf, sizeof(buf), 0) <= 0) {
            ok = 0;
        }
    }

    struct vnode *nul = vfs_resolve("/dev/null");
    if (!nul || nul->type != VNODE_CHARDEV) {
        ok = 0;
    }
    if (!vfs_resolve("/dev/zero")) {
        ok = 0;
    }
    if (!vfs_resolve("/bin/init.hxe")) {
        ok = 0;
    }
    if (vfs_resolve("/does/not/exist")) {
        ok = 0;
    }

    struct vnode *root = vfs_resolve("/");
    int found_bin = 0, found_etc = 0;
    if (root) {
        struct dirent d;
        for (uint64_t i = 0; i < 64 && vnode_readdir(root, i, &d) == 0; i++) {
            if (strcmp(d.name, "bin") == 0) found_bin = 1;
            if (strcmp(d.name, "etc") == 0) found_etc = 1;
        }
    }
    if (!found_bin || !found_etc) {
        ok = 0;
    }

    if (ok) {
        kernel_log_line("[PASS] kernel vfs");
    } else {
        kernel_log_error("kernel vfs test failed");
    }
}
