/* ramfs: a read-only in-memory tree built from the HXF1 initramfs. Files point
 * directly at the archive blobs (no copy); directories are synthesized from the
 * '/'-separated entry paths. This is the root filesystem mounted at "/". */
#ifndef MYOS_FS_RAMFS_H
#define MYOS_FS_RAMFS_H

struct filesystem;

/* Build a ramfs tree from the loaded initramfs and return the filesystem.
 * Returns NULL if the initramfs is unavailable. */
struct filesystem *ramfs_create_from_initramfs(void);

#endif /* MYOS_FS_RAMFS_H */
