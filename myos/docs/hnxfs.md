# HNXFS1 Persistent Filesystem

A compact custom filesystem (not ext/FAT/etc.) on a block device, mounted at
`/disk`. Magic `0x315346584E48` ("HNXFS1"). 4 KiB blocks (= 8 × 512-byte
sectors).

## On-disk layout (`hnxfs_format.h`)

```
block 0            superblock
block 1            inode bitmap   (1 bit / inode)
block 2            data bitmap    (1 bit / data block)
block 3 ..         inode table    (32 x 128-byte inodes per block)
data_block_start.. data blocks    (root directory in the first one)
```

* **superblock**: magic, version, block_size, total_blocks, inode_count, bitmap
  block numbers, inode-table location/size, data region, root inode (1).
* **inode** (128 B): type (free/file/dir), mode, size, block count, 12 direct
  block pointers (48 KiB max file in v0), mtime.
* **dirent** (128 B): inode number (0 = empty) + 120-byte name.

## Kernel driver

`hnxfs_mount(dev)` reads + validates the superblock and returns a `struct
filesystem` for the VFS. Helpers split across the required files:

* `hnxfs_inode.c` — read/write inode table entries.
* `hnxfs_alloc.c` — inode/data bitmap allocation (a shared zero block keeps 4 KiB
  buffers off the kernel stack).
* `hnxfs_dir.c` — directory lookup/add/remove/iterate over packed dirents.
* `hnxfs_file.c` — direct-block file read/write/truncate.
* `hnxfs.c` — VFS vnode ops (read/write/readdir/create/unlink), a small inode→
  vnode cache, and path lookup from the root inode.

All block access goes through the cached `block_read`/`block_write`, so writes
are write-through to the AHCI disk — data **persists** to `storage.img`.

## Operations + markers

format (host) · mount · create · write · read · mkdir · unlink · stat · readdir.
Markers: `[OK] HNXFS mounted at /disk`, `[PASS] hnxfs create file`,
`[PASS] hnxfs write file`, `[PASS] hnxfs read file`, `[PASS] hnxfs mkdir`,
`[PASS] hnxfs unlink`.

Tools: `tools/fs/mkhnxfs.py` (formatter), `tools/fs/inspect_hnxfs.py`.
