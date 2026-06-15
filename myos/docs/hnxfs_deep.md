# HNXFS (Deep Dive)

HNXFS1 is HNX/MyOS's persistent on-disk filesystem — a compact custom format (not
ext, FAT, or anything off-the-shelf). This document describes the on-disk layout
(superblock, bitmaps, inode table, directory entries, data blocks), how the Python
formatter `tools/fs/mkhnxfs.py` lays an image out, how the in-kernel driver mounts
the filesystem and implements read/write/mkdir/unlink, and how it sits on top of
the block layer.

Everything is grounded in `kernel/fs/hnxfs/` (`hnxfs_format.h`, `hnxfs.c`,
`hnxfs_inode.c`, `hnxfs_alloc.c`, `hnxfs_dir.c`, `hnxfs_file.c`, `hnxfs.h`) and the
tools `tools/fs/mkhnxfs.py` and `tools/fs/inspect_hnxfs.py`, plus the boot wiring in
`kernel/src/kernel.c`.

## Architecture

HNXFS uses 4 KiB logical blocks, each spanning 8 512-byte device sectors. The image
is divided into fixed regions described by the superblock:

```
block 0                : superblock
inode_bitmap_block     : 1 block, bit i = inode i in use
data_bitmap_block      : 1 block, bit j = data block j in use
inode_table_block ...  : inode table (32 inodes per 4 KiB block)
data_block_start ...   : file/dir data blocks
```

The driver layers on top:

```
VFS (vfs_resolve / fd ops)
   │  hnxfs.fs.lookup → hnxfs_lookup → hnxfs_get_vnode (cached)
   ▼
vnode_ops g_hnxfs_ops {v_read, v_write, v_readdir, v_create, v_unlink}
   │
   ├─ hnxfs_file.c   read/write/truncate over 12 direct blocks
   ├─ hnxfs_dir.c    128-byte dir entries, lookup/add/remove/get
   ├─ hnxfs_inode.c  read/write one 128-byte inode from the table
   └─ hnxfs_alloc.c  bitmap alloc/free for inodes and data blocks
   │
   ▼  hnxfs_read_block / hnxfs_write_block (4 KiB = 8 sectors)
block layer: block_read / block_write (cached, in block_device.h)
   ▼
struct block_device (AHCI/NVMe/partition) → physical disk
```

Files are addressed by **12 direct block pointers** only (no indirect blocks), so a
file is at most `12 * 4 KiB = 48 KiB`. Directories store **fixed 128-byte entries**,
32 per block. The format header and both the Python formatter and the kernel driver
share the same constants from `hnxfs_format.h`, so the two sides cannot drift.

## File map

| File | Role |
| --- | --- |
| `kernel/fs/hnxfs/hnxfs_format.h` | On-disk format: magic, sizes, superblock, inode, dirent structs, type enum |
| `kernel/fs/hnxfs/hnxfs.h` | In-kernel `struct hnxfs`, mount + block I/O + vnode-cache + sub-module API |
| `kernel/fs/hnxfs/hnxfs.c` | Mount, 4 KiB block I/O, vnode cache, vnode ops, path lookup |
| `kernel/fs/hnxfs/hnxfs_inode.c` | Read/write a single inode from the inode table |
| `kernel/fs/hnxfs/hnxfs_alloc.c` | Inode + data-block bitmap allocation/free |
| `kernel/fs/hnxfs/hnxfs_dir.c` | Directory entries: lookup/add/remove/get/empty |
| `kernel/fs/hnxfs/hnxfs_file.c` | File data: read/write/truncate over direct blocks |
| `kernel/fs/hnxfs/hnxfs_alloc.h` / `hnxfs_dir.h` / `hnxfs_file.h` | Thin headers re-including `hnxfs.h` |
| `tools/fs/mkhnxfs.py` | Format a raw HNXFS1 image (superblock, bitmaps, root dir) |
| `tools/fs/inspect_hnxfs.py` | Dump a superblock + root directory listing |
| `kernel/block/block_device.h` | `block_read`/`block_write`, `struct block_device` |
| `kernel/src/kernel.c` | Boot wiring: `hnxfs_mount(disk0p1)`, mount at `/disk` |

## Data structures

### On-disk format (`hnxfs_format.h`)

```c
#define HNXFS_MAGIC       0x315346584E48ULL   /* "HNXFS1" */
#define HNXFS_VERSION     1u
#define HNXFS_BLOCK_SIZE  4096u
#define HNXFS_SECTORS_PER_BLOCK (HNXFS_BLOCK_SIZE / 512u)   /* 8 */
#define HNXFS_DIRECT      12
#define HNXFS_NAME_MAX    120
#define HNXFS_ROOT_INODE  1
#define HNXFS_INODE_SIZE  128
#define HNXFS_INODES_PER_BLOCK  (4096 / 128)   /* 32 */
#define HNXFS_DIRENTS_PER_BLOCK (4096 / 128)   /* 32 */

enum { HNXFS_TYPE_FREE = 0, HNXFS_TYPE_FILE = 1, HNXFS_TYPE_DIR = 2 };
```

#### Superblock (block 0)

```c
struct hnxfs_superblock {
    uint64_t magic;                 /* HNXFS_MAGIC */
    uint32_t version;               /* HNXFS_VERSION */
    uint32_t block_size;            /* 4096 */
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_block;    /* block 1 */
    uint64_t data_bitmap_block;     /* block 2 */
    uint64_t inode_table_block;     /* block 3 */
    uint64_t inode_table_blocks;    /* 8 (256 inodes) */
    uint64_t data_block_start;      /* first data block (= 3 + 8 = 11) */
    uint64_t data_block_count;
    uint64_t root_inode;            /* 1 */
};
```

#### Inode (128 bytes, packed 32 per block)

```c
struct hnxfs_inode {
    uint32_t type;                  /* HNXFS_TYPE_* */
    uint32_t mode;
    uint64_t size;                  /* bytes (file) / entries-area bytes (dir) */
    uint64_t blocks;                /* allocated data block count */
    uint64_t direct[HNXFS_DIRECT];  /* 12 absolute block numbers */
    uint64_t mtime;                 /* reserved */
};
```

`direct[]` holds **absolute** block numbers (not data-region-relative indices). A
`0` entry is a hole. The byte layout is `IIQQ` + 12×`Q` + `Q` = 4+4+8+8+96+8 = 128
bytes, matching `INODE_SIZE` exactly.

#### Directory entry (128 bytes, packed 32 per block)

```c
struct hnxfs_dirent {
    uint64_t inode;                 /* 0 = empty slot */
    char     name[HNXFS_NAME_MAX];  /* 120 bytes, NUL-padded */
};
```

`8 + 120 = 128` bytes. A `0` inode marks an empty slot. Names are at most 119 chars
plus the terminator.

### In-kernel mount state (`hnxfs.h`)

```c
#define HNXFS_VNODE_CACHE 64
struct hnxfs {
    struct block_device     *dev;
    struct hnxfs_superblock  sb;        /* cached copy of block 0 */
    struct filesystem        fs;        /* VFS handle (name "hnxfs", lookup) */
    int                      mounted;
    struct vnode             vnodes[HNXFS_VNODE_CACHE];
    uint64_t                 vnode_inode[HNXFS_VNODE_CACHE];
    int                      vnode_count;
};
extern struct hnxfs g_hnxfs;
```

A single global mount (`g_hnxfs`) — v0 supports one HNXFS at a time. It caches the
superblock and up to 64 vnodes keyed by inode number. Each cached `struct vnode`
stores its inode number in `priv` (cast through `uintptr_t`), which is how every
vnode op recovers the inode it operates on.

## Key APIs

### Mount + block I/O (`hnxfs.h` / `hnxfs.c`)

- `struct filesystem *hnxfs_mount(struct block_device *dev)` — read block 0, verify
  magic/version, cache the superblock, fill `g_hnxfs.fs` (`name = "hnxfs"`,
  `lookup = hnxfs_lookup`, `data = &g_hnxfs`), return the filesystem (or `NULL`).
- `int hnxfs_read_block(uint64_t fsblk, void *buf)` /
  `int hnxfs_write_block(uint64_t fsblk, const void *buf)` — translate a 4 KiB FS
  block to `fsblk * 8` sectors and call `block_read`/`block_write` for 8 sectors.
- `struct vnode *hnxfs_get_vnode(uint64_t inode_num)` — return the cached vnode for
  an inode (refreshing type/size), or bind a new cache slot.

### Inode table (`hnxfs_inode.c`)

- `int hnxfs_read_inode(uint64_t num, struct hnxfs_inode *out)` — bounds-check
  (`num != 0`, `num < inode_count`), compute the table block and intra-block index,
  read the block, copy out the 128-byte inode.
- `int hnxfs_write_inode(uint64_t num, const struct hnxfs_inode *in)` —
  read-modify-write the table block.

### Bitmap allocation (`hnxfs_alloc.c`)

- `uint64_t hnxfs_alloc_inode(void)` — first free bit at index ≥ 2 in the inode
  bitmap (0 reserved, 1 = root); returns the inode number or 0.
- `void hnxfs_free_inode(uint64_t num)` — clear the bit (only for `num >= 2`).
- `uint64_t hnxfs_alloc_block(void)` — first free bit in the data bitmap; returns
  the **absolute** block number `data_block_start + i`, after zeroing the block.
- `void hnxfs_free_block(uint64_t absblk)` — clear the data bitmap bit for
  `absblk - data_block_start`.

### Directories (`hnxfs_dir.c`)

- `uint64_t hnxfs_dir_lookup(uint64_t dir_inode, const char *name)` — return the
  child inode or 0.
- `int hnxfs_dir_add(uint64_t dir_inode, const char *name, uint64_t child)` — fill a
  free slot, growing the directory by one data block if needed.
- `int hnxfs_dir_remove(uint64_t dir_inode, const char *name)` — clear the matching
  slot (`inode = 0`, `name[0] = 0`).
- `int hnxfs_dir_get(uint64_t dir_inode, uint64_t index, struct hnxfs_dirent *out)`
  — return the `index`-th non-empty, non-dot entry.
- `int hnxfs_dir_empty(uint64_t dir_inode)` — true iff `hnxfs_dir_get(dir, 0, ...)`
  finds nothing.

### File data (`hnxfs_file.c`)

- `int64_t hnxfs_file_read(uint64_t inode_num, void *buf, uint64_t size, uint64_t offset)`.
- `int64_t hnxfs_file_write(uint64_t inode_num, const void *buf, uint64_t size, uint64_t offset)`.
- `void hnxfs_file_truncate(uint64_t inode_num)` — free all direct blocks, zero size
  and block count.

## On-disk layout, formatted by mkhnxfs.py

`tools/fs/mkhnxfs.py` produces a raw image whose constants mirror
`hnxfs_format.h`. With the defaults (`--size-mb 8`, `INODE_TABLE_BLOCKS = 8`):

| Block(s) | Region | Contents |
| --- | --- | --- |
| 0 | Superblock | `pack_superblock(...)`: magic, version 1, block_size 4096, totals, region pointers, `root_inode = 1` |
| 1 | Inode bitmap | byte 0 = `0b00000011` (inode 0 reserved, inode 1 = root in use) |
| 2 | Data bitmap | byte 0 = `0b00000001` (data block 0 used by the root directory) |
| 3 .. 10 | Inode table | 8 blocks × 32 inodes = 256 inodes; inode 1 = the root directory |
| 11 .. | Data blocks | `data_block_start = 3 + 8 = 11`; the first holds the root directory |

`pack_superblock` writes the region pointers literally: `inode_bitmap_block = 1`,
`data_bitmap_block = 2`, `inode_table_block = 3`, `inode_table_blocks = 8`,
`data_start = 11`, `root_inode = 1`. `inode_count = INODE_TABLE_BLOCKS *
INODES_PER_BLOCK = 256`. `data_count = total_blocks - data_start`.

The **root inode** (number 1) is written as a directory with one data block:
`pack_inode(TYPE_DIR, BLOCK, 1, [data_start])` — `type = 2`, `mode = 0o755`,
`size = 4096`, `blocks = 1`, `direct[0] = 11`. (Note the formatter writes mode
`0o755` while the kernel's `v_create` writes mode `0644` for new nodes; mode is
advisory.)

The **root directory data block** (block 11) is seeded with two 128-byte entries:
`pack_dirent(1, ".")` and `pack_dirent(1, "..")`, both pointing at inode 1. The
in-kernel `hnxfs_dir_get` skips `.`/`..` (via `is_dot`), so they never appear in a
`readdir`, but they keep the directory non-empty in the on-disk sense.

The formatter prints `[OK] HNXFS image: <out> (<blocks> blocks, <inodes> inodes,
<data blocks> data blocks)`. `inspect_hnxfs.py` reads the same `<QIIQQQQQQQQQ`
superblock layout (supporting an `--offset` into a whole-disk image), then walks the
root inode → `direct[0]` → directory entries to list the root.

The build pipeline (`make storage-image`) runs `mkhnxfs.py --out hnxfs.img
--size-mb 8`, then `mkdisk.py` wraps it as partition 1 of a disk image; QEMU
attaches it, the kernel discovers `disk0p1`, and mounts it at `/disk`.

## In-kernel mount path

`hnxfs_mount(dev)` (`hnxfs.c`):

1. `block_read(dev, 0, buf, HNXFS_SECTORS_PER_BLOCK)` — read the 4 KiB superblock
   (8 sectors). On I/O error, return `NULL`.
2. Validate `sb->magic == HNXFS_MAGIC` and `sb->version == HNXFS_VERSION`; on
   mismatch log `hnxfs: bad superblock magic/version` and return `NULL`.
3. Cache: `g_hnxfs.dev = dev`, `g_hnxfs.sb = *sb`, reset the vnode cache,
   `mounted = 1`, fill `g_hnxfs.fs`.
4. Return `&g_hnxfs.fs`.

The VFS then mounts it at `/disk` (`vfs_mount("/disk", hfs, NULL)`), logging
`[OK] HNXFS mounted at /disk`. From `kernel_main`:

```c
struct block_device *root_part = block_get_device("disk0p1");
if (root_part) {
    struct filesystem *hfs = hnxfs_mount(root_part);
    if (hfs && vfs_mount("/disk", hfs, NULL) == 0) kernel_log_ok("HNXFS mounted at /disk");
    else kernel_log_error("hnxfs: mount failed");
} else kernel_log_error("hnxfs: disk0p1 not found");
```

### Block I/O sits on the block layer

`hnxfs_read_block`/`hnxfs_write_block` are pure translations: a 4 KiB FS block is
`fsblk * HNXFS_SECTORS_PER_BLOCK` (×8) sectors. They call `block_read`/`block_write`
(`kernel/block/block_device.h`), which route through the block cache and the
underlying `struct block_device` (an AHCI/NVMe disk or a partition device). HNXFS
never talks to hardware directly; the block layer owns DMA and caching.

## Path lookup and the vnode cache

`hnxfs_lookup(fs, rel)` walks the relative path from `HNXFS_ROOT_INODE` (1),
tokenizing on `/`, and for each component calls `hnxfs_dir_lookup(inode, comp)`. A
missing component returns `NULL`; otherwise the final inode is turned into a vnode
by `hnxfs_get_vnode`.

`hnxfs_get_vnode(inode_num)`:

- If the inode is already cached, refresh its `type` and `size` from disk and return
  the cached vnode.
- Otherwise take a new slot (`vnode_count++` while under 64, else reuse
  `inode_num % HNXFS_VNODE_CACHE`), read the inode (failure → `NULL`), and fill the
  vnode: `type` from `type_of`, `size`, `ops = &g_hnxfs_ops`, `fs = &g_hnxfs.fs`,
  `priv = (void *)(uintptr_t)inode_num`.

`type_of(hnxfs_type)` maps `HNXFS_TYPE_DIR` → `VNODE_DIR`, everything else →
`VNODE_FILE`.

## vnode operations (`g_hnxfs_ops`)

```c
static const struct vnode_ops g_hnxfs_ops = { v_read, v_write, v_readdir, v_create, v_unlink };
```

Each op recovers its inode from `vn->priv` / `dir->priv`.

- **`v_read`** → `hnxfs_file_read(inode, buf, size, offset)`.
- **`v_write`** → `hnxfs_file_write(...)`, then refresh `vn->size` from the on-disk
  inode so a subsequent `lseek(SEEK_END)` sees the new length.
- **`v_readdir`** → `hnxfs_dir_get(inode, index, &de)`; fill the `dirent` name, then
  read the child inode to report its `size` and mapped `type` (defaults to a 0-byte
  file if the child inode cannot be read).
- **`v_create`** (file or dir): reject if the name already exists (`-SYS_EEXIST`);
  `hnxfs_alloc_inode` (else `-SYS_ENOMEM`); write a fresh inode with the right type
  and `mode = 0644`; `hnxfs_dir_add` (rolling back the inode on failure); optionally
  return the new vnode via `hnxfs_get_vnode`.
- **`v_unlink`**: look up the child (`-SYS_ENOENT` if absent); read its inode; refuse
  to remove a non-empty directory (`-SYS_EINVAL` when `!hnxfs_dir_empty`);
  `hnxfs_file_truncate` (free data blocks), `hnxfs_free_inode`, then
  `hnxfs_dir_remove`.

These are reached from the VFS: `vfs_mkdir`/`vfs_create` call `ops->create` and
`vfs_unlink` calls `ops->unlink` on the parent directory vnode (see
`docs/vfs_deep.md`).

## File read / write / truncate

### Read (`hnxfs_file_read`)

Reads the inode, clamps `size` to `in.size - offset` (returns 0 past EOF), then
loops block-by-block. For each step it computes the block index `pos /
HNXFS_BLOCK_SIZE`, the intra-block offset, and the chunk length. A missing block
(`bidx >= in.blocks` or `direct[bidx] == 0`) is treated as a **sparse hole** and
zero-filled; otherwise the block is read and `memcpy`'d. A read failure returns the
bytes already copied.

### Write (`hnxfs_file_write`)

Reads the inode, then loops. For each step it computes the block index; if `bidx >=
HNXFS_DIRECT` (12) it stops (max file size reached). If `direct[bidx] == 0` it
`hnxfs_alloc_block`s a new block (stopping if allocation fails) and grows `in.blocks`
when needed. It does a read-modify-write of the target block (`hnxfs_read_block`,
patch the chunk, `hnxfs_write_block`). After the loop it extends `in.size` if the
write went past the old end and `hnxfs_write_inode`s the updated inode. Returns the
bytes written (which may be short if a block could not be allocated or the 48 KiB
limit was hit).

### Truncate (`hnxfs_file_truncate`)

Frees every non-zero `direct[]` block (`hnxfs_free_block`), zeroes the entries, sets
`blocks = 0` and `size = 0`, and writes the inode back. Used by `v_unlink` before
freeing the inode.

## Directory mutation

Directories are arrays of 128-byte `hnxfs_dirent` slots across up to 12 data blocks.

- **`hnxfs_dir_lookup`** scans every slot of every direct block for a matching,
  non-empty entry (`name_eq` is a bounded `strncmp` over `HNXFS_NAME_MAX`).
- **`hnxfs_dir_add`** first scans existing blocks for a free slot
  (`de->inode == 0`); if found, writes the entry there. Otherwise, if the directory
  has fewer than 12 blocks, it `hnxfs_alloc_block`s a new directory block, appends it
  to `direct[]`, updates `blocks` and `size = blocks * 4096`, writes the inode, then
  seeds the new block with the single entry. A full directory (12 blocks, all slots
  used) returns `-SYS_ENOMEM`.
- **`hnxfs_dir_remove`** finds the matching slot and clears it.
- **`hnxfs_dir_get`** returns the `index`-th entry that is non-empty and not `.`/`..`
  (so `readdir` hides the dot entries).

## Sizing and limits

- **Block size:** 4 KiB (8 × 512-byte sectors).
- **Max file size:** `HNXFS_DIRECT * HNXFS_BLOCK_SIZE = 12 * 4096 = 48 KiB`
  (no indirect blocks).
- **Inodes:** `inode_table_blocks * 32`; the formatter default is `8 * 32 = 256`.
  Inode 0 is reserved, inode 1 is the root.
- **Directory entries:** 32 per block; up to `12 * 32 = 384` slots per directory
  (minus the `.`/`..` entries in the first block).
- **Names:** ≤ 119 chars + NUL (`HNXFS_NAME_MAX = 120`).
- **Vnode cache:** 64 entries (`HNXFS_VNODE_CACHE`); one mounted HNXFS at a time.
- **Stack discipline:** every block buffer is a 4 KiB `uint8_t[HNXFS_BLOCK_SIZE]`
  local. `hnxfs_alloc_block` deliberately uses a shared, never-written static zero
  page to zero a freshly allocated block, to keep an extra 4 KiB buffer off the
  (already deeply nested) kernel stack.

## Invariants

- **The superblock is verified at mount.** `magic == HNXFS_MAGIC` and
  `version == HNXFS_VERSION`, else the mount fails.
- **Inode 0 is reserved; inode 1 is always the root directory.** `hnxfs_alloc_inode`
  starts allocating at index 2; `hnxfs_free_inode` ignores 0 and 1; `hnxfs_lookup`
  starts at inode 1.
- **`direct[]` holds absolute block numbers.** `hnxfs_alloc_block` returns
  `data_block_start + i`; `hnxfs_free_block` subtracts it back to index the bitmap.
- **A vnode's `priv` is its inode number.** Every vnode op casts `priv` back through
  `uintptr_t` to recover the inode.
- **The on-disk struct sizes are exact.** `hnxfs_inode` and `hnxfs_dirent` are each
  128 bytes, packed `HNXFS_INODES_PER_BLOCK` / `HNXFS_DIRENTS_PER_BLOCK` (32) per
  4 KiB block; the formatter packs the same byte layout.
- **A 4 KiB FS block is always 8 device sectors.** `hnxfs_read_block`/`_write_block`
  multiply by `HNXFS_SECTORS_PER_BLOCK`.
- **Holes read as zeros.** `hnxfs_file_read` zero-fills missing blocks rather than
  failing; `direct[bidx] == 0` is a valid sparse hole.
- **Files cap at 48 KiB.** `hnxfs_file_write` stops at `bidx >= HNXFS_DIRECT`.
- **Non-empty directories are not removed.** `v_unlink` checks `hnxfs_dir_empty`
  before unlinking a directory; `.`/`..` do not count (they are filtered by
  `hnxfs_dir_get`).
- **The format constants are single-sourced.** `hnxfs_format.h` is the authority;
  `mkhnxfs.py` and `inspect_hnxfs.py` replicate the same numbers and pack/unpack
  strings.

## Failure modes

| Condition | Result |
| --- | --- |
| Superblock read I/O error | `hnxfs_mount` returns `NULL` |
| Bad magic / version | `hnxfs: bad superblock magic/version`, `NULL` |
| `disk0p1` not found at boot | `hnxfs: disk0p1 not found` (no `/disk`) |
| Mount or `vfs_mount` fails | `hnxfs: mount failed` (no `/disk`) |
| Inode number 0 or ≥ `inode_count` | `hnxfs_read_inode`/`hnxfs_write_inode` return `-SYS_EINVAL` |
| Block read/write error in inode access | `-SYS_EIO` |
| `create` of an existing name | `-SYS_EEXIST` |
| Inode bitmap full | `hnxfs_alloc_inode` returns 0 → `v_create` `-SYS_ENOMEM` |
| Data bitmap full (write/dir grow) | short write / `hnxfs_dir_add` `-SYS_ENOMEM` |
| Write past 48 KiB | write stops at 12 blocks (short count) |
| Directory full (12 blocks, no free slot) | `hnxfs_dir_add` `-SYS_ENOMEM` |
| `unlink` of a missing name | `-SYS_ENOENT` |
| `unlink` of a non-empty directory | `-SYS_EINVAL` |
| `mkdir`/`unlink` via VFS on a read-only fs | `-SYS_EPERM` (handled at the VFS layer) |

## Verification

- **`make verify-hnxfs`** — boots the image and expects the full HNXFS marker set:
  `[OK] HNXFS mounted at /disk`, `[PASS] hnxfs create file`,
  `[PASS] hnxfs write file`, `[PASS] hnxfs read file`, `[PASS] hnxfs mkdir`,
  `[PASS] hnxfs unlink`. These exercise the entire write path: `v_create` →
  `hnxfs_alloc_inode` + `hnxfs_dir_add`, `v_write` → `hnxfs_file_write` +
  `hnxfs_alloc_block`, `v_read`, `mkdir`, and `v_unlink` → truncate + free + remove.
- **`make verify-storage`** — expects `[OK] AHCI block device online`,
  `[PASS] disk read`, `[PASS] disk write` (the block layer HNXFS rides on).
- **`make verify-block`** — expects `[OK] Block layer online`, `[PASS] block cache`,
  `[PASS] partition parser` (the cache and partition that surface `disk0p1`).
- **`make storage-image`** — builds `hnxfs.img` via `mkhnxfs.py` (prints
  `[OK] HNXFS image: ...`) and wraps it into the disk images via `mkdisk.py`.

The image-side serial markers and the build-side `[OK] HNXFS image` line together
prove the formatter and the driver agree on the layout. `tools/fs/inspect_hnxfs.py`
can be run against the built `build/image/hnxfs.img` (or a whole-disk image with
`--offset`) to dump the superblock and root directory for manual inspection.

## Future expansion

- **Indirect blocks** to lift the 48 KiB file cap — `struct hnxfs_inode` would gain
  single/double indirect pointers, and `hnxfs_file_read`/`_write` a block-map walk.
- **Multiple mounts.** `g_hnxfs` is a single global; a per-mount `struct hnxfs`
  passed via `fs->data` would allow several HNXFS volumes at once.
- **`mtime` and real `mode`.** The inode already reserves `mtime`; the formatter
  (`0o755`) and the driver (`0644`) disagree on default mode because mode is
  currently advisory.
- **Crash consistency.** Writes are read-modify-write with no journaling or
  ordering guarantees; a journal or copy-on-write scheme would make the filesystem
  power-fail safe.
- **`rename`/`link`.** Directory ops cover lookup/add/remove/get only; a rename
  would compose remove+add atomically, and hard links would need a link count on the
  inode.
- **Larger inode tables / dynamic sizing.** `INODE_TABLE_BLOCKS` is fixed at format
  time (256 inodes by default); growing it requires reformatting.
- **A free-space cursor.** `bitmap_alloc` scans from the start each call; a hint
  cursor would speed allocation on a full-ish volume.
