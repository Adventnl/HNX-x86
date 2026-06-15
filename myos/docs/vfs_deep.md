# HNX/MyOS Virtual Filesystem (Deep Dive)

This document describes the HNX/MyOS virtual filesystem (VFS): its object model
(filesystem, vnode, file, mount), how paths are normalized and resolved, the
longest-prefix mount table, the open-file abstraction and open flags, and the two
in-tree backends — **ramfs** (the read-only root) and **devfs** (the synthetic
`/dev` tree). The persistent on-disk backend, HNXFS, plugs into the same model and
is documented separately in `docs/hnxfs_deep.md`.

Everything is grounded in `kernel/fs/`: `vfs.c/.h`, `inode.c/.h`, `file.c/.h`,
`path.c/.h`, `ramfs/ramfs.c`, and `devfs/devfs.c`, plus the boot wiring in
`kernel/src/kernel.c`.

## Architecture

The VFS is a small "switch" over pluggable filesystems. There is no global
namespace tree of vnodes; instead a flat **mount table** maps absolute path
prefixes to filesystems, and each filesystem implements a single `lookup` callback
that resolves a path *relative to its own root* into a `struct vnode`.

```
absolute path "/dev/console"
   │  path_resolve  (normalize: collapse "." "..", repeated "/")
   ▼
vfs_resolve("/dev/console")
   │  find_mount: longest-prefix match in g_mounts → /dev (devfs)
   │  rel = "console"
   ▼
devfs lookup(fs, "console") → struct vnode * (chardev, priv = char_device)
   │
   ▼
fd-based ops act through struct file → vnode_read / vnode_write / vnode_readdir
   │  which dispatch to vn->ops (the backend's vnode_ops)
   ▼
backend implementation (ramfs blob copy, devfs char_device, hnxfs block I/O)
```

Three object layers stack up:

1. **`struct filesystem`** — a named backend with a `lookup` callback and private
   `data`. ramfs, devfs, and hnxfs each provide one.
2. **`struct vnode`** — the in-memory handle for one file, directory, or character
   device. It carries a `type`, a `size`, a pointer to its backend's `vnode_ops`,
   a back-pointer to its `filesystem`, and a `priv` pointer to the backend's own
   node object.
3. **`struct file`** — a refcounted open instance: `(vnode, offset, flags, path)`.
   File descriptors in a process's fd table point at these.

Path strings are the only "names" the VFS handles — there is no separate dentry
cache. Each backend's `lookup` re-walks the relative path on every resolve. (HNXFS
adds a small vnode cache keyed by inode number; see its own doc.)

## File map

| File | Role |
| --- | --- |
| `kernel/fs/vfs.h` | `struct filesystem`, mount/resolve/fd-op API, `VFS_MAX_MOUNTS` |
| `kernel/fs/vfs.c` | Mount table, longest-prefix match, fd-based open/read/write/lseek/readdir/stat, mkdir/create/unlink, mount introspection |
| `kernel/fs/inode.h` | `struct vnode`, `struct vnode_ops`, `struct dirent`, `struct stat`, `enum vnode_type`; dispatch wrappers |
| `kernel/fs/inode.c` | `vnode_read` / `vnode_write` / `vnode_readdir` (NULL-op safe wrappers) |
| `kernel/fs/file.h` | `struct file`, alloc/ref/unref |
| `kernel/fs/file.c` | Refcounted open-file lifecycle |
| `kernel/fs/path.h` | `path_resolve`, `path_basename`, `path_parent` |
| `kernel/fs/path.c` | Path normalization with a component stack |
| `kernel/fs/ramfs/ramfs.c` / `.h` | Read-only tree built from the initramfs (root fs at `/`) |
| `kernel/fs/devfs/devfs.c` / `.h` | Synthetic `/dev` over the char-device registry |
| `kernel/src/kernel.c` | Boot wiring: `vfs_init`, mount ramfs at `/`, devfs at `/dev`, hnxfs at `/disk` |

## Data structures

### `struct filesystem` (`vfs.h`)

```c
struct filesystem {
    const char *name;
    struct vnode *(*lookup)(struct filesystem *fs, const char *rel_path);
    void *data;            /* fs-private state */
};
```

`lookup` takes the path remainder *after* the mount prefix has been stripped
(`""` means the filesystem root) and returns a vnode owned by the filesystem, or
`NULL` if absent. `data` is fs-private state (e.g. hnxfs stores its `struct hnxfs`
there).

### `struct vnode` and `struct vnode_ops` (`inode.h`)

```c
enum vnode_type { VNODE_FILE = 0, VNODE_DIR = 1, VNODE_CHARDEV = 2 };

struct vnode {
    enum vnode_type        type;
    uint64_t               size;
    const struct vnode_ops *ops;
    struct filesystem      *fs;
    void                   *priv;    /* fs-specific node pointer */
};

struct vnode_ops {
    int64_t (*read)(struct vnode *vn, void *buf, uint64_t size, uint64_t offset);
    int64_t (*write)(struct vnode *vn, const void *buf, uint64_t size, uint64_t offset);
    int (*readdir)(struct vnode *vn, uint64_t index, struct dirent *out);
    int (*create)(struct vnode *dir, const char *name, enum vnode_type type,
                  struct vnode **out);
    int (*unlink)(struct vnode *dir, const char *name);
};
```

`read`/`write` transfer at an absolute byte offset and return bytes moved or
`-errno`. `readdir` fills the `index`-th child of a directory (0 on success,
negative when past the end). `create`/`unlink` mutate a directory and exist only on
writable filesystems (ramfs and devfs leave them `NULL`).

### `struct dirent` and `struct stat` (`inode.h`)

```c
#define VFS_NAME_MAX 128
#define VFS_PATH_MAX 256

struct dirent { char name[VFS_NAME_MAX]; uint64_t size; uint32_t type; };
struct stat   { uint64_t size; uint32_t type; uint32_t mode; };
```

`mode` is advisory in v0. `type` holds an `enum vnode_type`.

### `struct file` (`file.h`)

```c
struct file {
    struct vnode *vnode;
    int64_t       offset;
    int           flags;
    int           refcount;
    char          path[VFS_PATH_MAX];   /* absolute path it was opened with */
};
```

The open-file object. The seek offset lives here (not on the vnode), so two open
instances of the same vnode have independent offsets. Refcount-shared instances
(e.g. stdio fds 0/1/2) share one offset. The vnode itself is owned by its
filesystem and is never freed by `file_unref`.

### Mount table (`vfs.c`)

```c
#define VFS_MAX_MOUNTS 8
struct mount { char path[VFS_PATH_MAX]; struct filesystem *fs; int used; };
static struct mount g_mounts[VFS_MAX_MOUNTS];
```

A fixed array of up to 8 mounts. No unmount in v0; mounts are added at boot and
stay for the lifetime of the system.

## Key APIs

### Mount and resolve (`vfs.h`)

- `void vfs_init(void)` — clear the mount table; logs `[OK] VFS online`.
- `int vfs_mount(const char *path, struct filesystem *fs, void *data)` — register
  `fs` at absolute `path` (must start with `/`); stores `data` in `fs->data` if
  non-NULL. Returns 0, `-SYS_EINVAL` (bad args), or `-SYS_ENOMEM` (table full).
- `struct vnode *vfs_resolve(const char *abspath)` — kernel-side resolve of an
  already-normalized absolute path to a vnode (longest-prefix mount + backend
  lookup). `NULL` if unmounted or absent.

### fd-based operations (operate on the current process) (`vfs.h`)

- `int vfs_open(const char *path, int flags)` — resolve (relative to the process
  cwd), optionally `O_CREAT`, check `O_DIRECTORY`, allocate a `struct file`, install
  in the fd table. Returns the fd or a negative errno.
- `int vfs_close(int fd)`.
- `int64_t vfs_read(int fd, void *buffer, uint64_t size)` /
  `vfs_write(int fd, const void *buffer, uint64_t size)` — transfer at the file's
  current offset and advance it on success.
- `int64_t vfs_lseek(int fd, int64_t offset, int whence)` — `SEEK_SET/CUR/END`.
- `int vfs_readdir(int fd, struct dirent *out)` — 1 = entry, 0 = end, <0 = error.
- `int vfs_stat(const char *path, struct stat *out)`.

### Namespace mutation (`vfs.h`)

- `int vfs_mkdir(const char *path)` / `int vfs_create(const char *path)` /
  `int vfs_unlink(const char *path)`. All resolve the parent directory and dispatch
  to the directory vnode's `ops->create` / `ops->unlink`; a read-only filesystem
  (NULL op) yields `-SYS_EPERM`.

### Introspection (`vfs.h`)

- `int vfs_mount_count(void)` — number of live mounts.
- `int vfs_mount_info(int index, char *path_out, ..., char *fs_out, ...)` — the
  `index`-th mount's path and filesystem name (backs the `mounts` coreutil and
  `SYS_MOUNT_INFO`).

### vnode dispatch wrappers (`inode.h` / `inode.c`)

`vnode_read`, `vnode_write`, `vnode_readdir` are NULL-op-safe wrappers that also
enforce type rules: `vnode_read`/`vnode_write` return `-SYS_EISDIR` on a directory
and `-SYS_EINVAL` if the op is missing; `vnode_readdir` returns `-SYS_ENOTDIR` on a
non-directory or a missing op.

### Path helpers (`path.h`)

- `int path_resolve(const char *cwd, const char *path, char *out, uint64_t out_size)`
  — normalize `path` (absolute, or relative to `cwd`) into `out`. Always begins with
  `/`. Returns 0 or a negative error.
- `const char *path_basename(const char *path)` — pointer to the final component
  (`"/"` → `""`).
- `int path_parent(const char *abspath, char *out, uint64_t out_size)` — the parent
  directory (`"/x"` → `"/"`, `"/"` → `"/"`).

## Path resolution and the name model

Paths are POSIX-style: `/`-separated, `/` is the root, no symlinks.

### Normalization (`path_resolve`)

`path_resolve` seeds a working buffer with `cwd` (unless `path` is absolute), then
tokenizes on `/` into a component stack of up to `MAX_COMPONENTS` (32). It applies:

- empty components and `"."` → skipped,
- `".."` → pop the stack (no-op at the root),
- anything else → push (a pointer + length into the working copy).

It then rebuilds `"/a/b/c"` (or `"/"` if the stack is empty). Overflow at any stage
(seed too long, too many components, output too long) returns `-SYS_ENAMETOOLONG`.
The result is always an absolute, collapsed path with no `.`/`..`/`//`.

User-facing operations resolve relative to the process cwd: `vfs_open`, `vfs_stat`,
and the mutating ops all call `path_resolve(process_cwd(p), path, abs, ...)` first.
`vfs_resolve` itself takes an *already-normalized* absolute path.

### Longest-prefix mount matching (`find_mount`)

`vfs_resolve` calls `find_mount`, which scans `g_mounts` for the mount whose path is
the longest prefix of the absolute path. The root mount (`"/"`) matches everything;
any other mount `mp` matches when `abspath` starts with `mp` followed by `/` or the
end of string (so `/dev` matches `/dev/console` but not `/device`). The chosen
mount strips its prefix (and any leading slashes) and passes the remainder to
`fs->lookup`. With ramfs at `/`, devfs at `/dev`, and hnxfs at `/disk`, a path like
`/dev/console` resolves to devfs with `rel = "console"`.

### The name model

There is no dentry/inode cache at the VFS layer. A "name" is just a path string;
each backend's `lookup` walks the relative path component-by-component every time.
This keeps the VFS tiny at the cost of re-walking on each resolve. Each backend has
its own component tokenizer:

- ramfs walks its in-memory `ramfs_node` tree (`find_child` per component).
- devfs treats the remainder as a single device name (no nesting under `/dev`).
- hnxfs walks the directory inode chain via `hnxfs_dir_lookup` per component.

## Open flags and `vfs_open`

Open flags come from `syscall_numbers.h`:

```
O_RDONLY 0x0000   O_WRONLY 0x0001   O_RDWR 0x0002
O_DIRECTORY 0x0010   O_CREAT 0x0040   O_TRUNC 0x0200
```

`vfs_open` (`vfs.c`):

1. Require a current process (else `-SYS_EFAULT`).
2. `path_resolve(process_cwd(p), path, abs, ...)`.
3. `vfs_resolve(abs)`. If it is `NULL` and `O_CREAT` is set, `vfs_create(abs)` then
   re-resolve. If still `NULL`, return `-SYS_ENOENT`.
4. If `O_DIRECTORY` is set and the vnode is not a directory, `-SYS_ENOTDIR`.
5. `O_TRUNC` is a no-op in v0 (the comment notes truncation is fs-specific; HNXFS
   files are overwritten from offset 0 by callers).
6. `file_alloc(vn, flags, abs)` → `fd_alloc(process_fds(p), f)`. On a full fd table
   the file is unref'd and the fd error is returned.

The access mode flags (`O_RDONLY`/`O_WRONLY`/`O_RDWR`) are stored on the `struct
file` but are not currently enforced on read/write in the VFS layer; enforcement is
left to the backend (e.g. ramfs `write` always returns `-SYS_EPERM`).

## Read / write / seek / readdir flow

All fd ops fetch the current process's fd table (`current_fds`) and the `struct
file` (`fd_get`); a bad fd returns `-SYS_EBADF`.

- **`vfs_read`** calls `vnode_read(f->vnode, buffer, size, f->offset)` and adds the
  returned count to `f->offset` on success.
- **`vfs_write`** calls `vnode_write(...)` and likewise advances the offset.
- **`vfs_lseek`** computes the new offset from `SEEK_SET` (0), `SEEK_CUR`
  (`f->offset`), or `SEEK_END` (`f->vnode->size`); a negative result is
  `-SYS_EINVAL`, an unknown whence is `-SYS_EINVAL`.
- **`vfs_readdir`** requires a directory vnode (`-SYS_ENOTDIR` otherwise), calls
  `vnode_readdir(f->vnode, f->offset, out)`, and on success advances `f->offset` by
  one and returns 1; past-the-end returns 0. The offset doubles as the directory
  iteration cursor.

`vfs_stat` resolves the path and fills `size`, `type`, and `mode = 0`.

## Namespace mutation (mkdir / create / unlink)

`vfs_mkdir` and `vfs_create` both call the helper `vfs_make(path, type)`:

1. `resolve_parent(path, &dir, base, ...)` — normalize, split into parent path +
   final component (`path_parent` + `path_basename`), resolve the parent vnode, and
   reject a non-directory parent (`-SYS_ENOTDIR`) or an attempt to mutate the root
   itself (empty basename → `-SYS_EINVAL`).
2. If the directory vnode has no `ops->create`, return `-SYS_EPERM` (read-only fs).
3. Otherwise `dir->ops->create(dir, base, type, NULL)`.

`vfs_unlink` is analogous but dispatches to `ops->unlink`. Because ramfs and devfs
leave `create`/`unlink` as `NULL`, all mutation under `/` and `/dev` returns
`-SYS_EPERM`; mutation succeeds only under a writable mount such as HNXFS at
`/disk`.

## The ramfs backend (root filesystem)

ramfs (`kernel/fs/ramfs/ramfs.c`) is a **read-only** in-memory tree built from the
HXF1 initramfs. It is mounted at `/` and is the root filesystem.

### Node model

```c
struct ramfs_node {
    char                name[VFS_NAME_MAX];
    enum vnode_type     type;
    const void         *data;          /* file blob (NULL for dirs) */
    uint64_t            size;
    struct ramfs_node  *first_child;
    struct ramfs_node  *next_sibling;
    struct vnode        vnode;         /* embedded; .priv -> this node */
};
```

Each node embeds its `struct vnode`, whose `priv` points back at the node. File
nodes point `data` directly at the initramfs archive blob — **no copy** is made.
Directories are synthesized from the `/`-separated entry paths.

### Operations

- `ramfs_read` — bounded `memcpy` from `data + offset`; returns 0 past EOF.
- `ramfs_write` — always `-SYS_EPERM` (read-only).
- `ramfs_readdir` — walk the sibling list to the `index`-th child.
- `create`/`unlink` are `NULL`.

Two op tables: `ramfs_file_ops = { read, write, NULL, NULL, NULL }` and
`ramfs_dir_ops = { NULL, NULL, readdir, NULL, NULL }`.

### Construction

`ramfs_create_from_initramfs()` (returns `NULL` if no initramfs) builds the root
node, then for each initramfs entry whose path starts with `/`, `insert_path`
creates the intermediate directories (`ensure_dir`) and the leaf file
(`node_new(..., VNODE_FILE, data, size)`). `find_child` avoids duplicates.
`ramfs_lookup` walks the tree component-by-component from `g_root`.

This is how `/bin/*.hxe`, `/tests/*.hxe`, and `/etc/*.txt` (e.g.
`/etc/banner.txt`) become visible: the bootloader loads the initramfs into RAM,
`initramfs_init` parses it, and ramfs exposes it through the VFS.

## The devfs backend (`/dev`)

devfs (`kernel/fs/devfs/devfs.c`) is a synthetic filesystem that maps the kernel's
character-device registry into the VFS at `/dev` (console, null, zero).

### Object model

- A single static root directory vnode `g_root` (`VNODE_DIR`, ops =
  `dev_root_ops`).
- Up to `DEVICE_MAX_CHAR` lazily-bound per-device vnodes (`g_dev_vnodes[]`), each
  `VNODE_CHARDEV` with `priv` pointing at the `struct char_device`.

### Operations

- Root directory: `dev_root_readdir` enumerates registered char devices via
  `device_char_at(index)`, reporting `type = VNODE_CHARDEV`, `size = 0`.
- Char device: `dev_read` / `dev_write` forward to `char_device_read` /
  `char_device_write` on `vn->priv` (offset is ignored — devices are byte streams).
  Op table `dev_ops = { dev_read, dev_write, NULL, NULL, NULL }`.

### Lookup and binding

`devfs_lookup(fs, rel)`: an empty remainder returns the root directory; otherwise
`device_find_char(rel)` locates the device by name and `bind_vnode` returns a stable
vnode for it (reusing an existing binding, or allocating a new slot up to
`DEVICE_MAX_CHAR`). `devfs_create()` resets the binding table and logs
`[OK] devfs online`.

This is what makes `/dev/console` resolvable, which the process model uses to wire
fds 0/1/2 for every new process.

## Boot wiring (`kernel/src/kernel.c`)

The VFS is brought up in `kernel_main` after the initramfs is parsed:

```c
vfs_init();
device_init();
console_init();
tty_init();

struct filesystem *rootfs = ramfs_create_from_initramfs();
if (!rootfs || vfs_mount("/", rootfs, NULL) != 0) kernel_panic("vfs: cannot mount root ramfs");
struct filesystem *devfs = devfs_create();
if (!devfs || vfs_mount("/dev", devfs, NULL) != 0) kernel_panic("vfs: cannot mount devfs");
```

Later, after the block layer and partition scan, HNXFS is mounted at `/disk`:

```c
struct block_device *root_part = block_get_device("disk0p1");
if (root_part) {
    struct filesystem *hfs = hnxfs_mount(root_part);
    if (hfs && vfs_mount("/disk", hfs, NULL) == 0) kernel_log_ok("HNXFS mounted at /disk");
}
```

So the live namespace is: `/` (ramfs, read-only), `/dev` (devfs), `/disk` (hnxfs,
read-write). At most 8 mounts (`VFS_MAX_MOUNTS`) can coexist.

## Invariants

- **Paths are normalized before resolve.** `vfs_resolve` requires an already-collapsed
  absolute path; user-facing entry points call `path_resolve` first.
- **Longest-prefix wins.** `find_mount` always selects the most specific mount; the
  root mount (`"/"`) is the universal fallback.
- **Mount prefixes match on a boundary.** A mount `mp` matches only when the next
  char is `/` or the string ends, so `/dev` never captures `/device`.
- **The offset lives on `struct file`, not the vnode.** Independent open instances
  have independent cursors; the directory cursor is the same field.
- **Vnodes are owned by their filesystem.** `file_unref` frees the `struct file`
  only; it never frees the vnode (the comment in `file.c` states this explicitly).
- **Read-only filesystems are read-only structurally.** ramfs/devfs leave
  `write`/`create`/`unlink` `NULL`; the wrappers and `vfs_make`/`vfs_unlink` turn a
  missing op into `-SYS_EPERM` (or `-SYS_EINVAL`), never a crash.
- **Directory reads are rejected as data reads.** `vnode_read`/`vnode_write` return
  `-SYS_EISDIR` for a `VNODE_DIR`.
- **fds 0/1/2 resolve through devfs.** The process layer depends on
  `/dev/console` being resolvable at process creation.
- **At most `VFS_MAX_MOUNTS` (8) mounts** and at most `MAX_COMPONENTS` (32) path
  components per resolve.

## Failure modes

| Condition | Result |
| --- | --- |
| `vfs_mount` with a non-`/` path or NULL fs | `-SYS_EINVAL` |
| `vfs_mount` with the table full (>8) | `-SYS_ENOMEM` |
| Resolve under an unmounted prefix | `vfs_resolve` returns `NULL` → `-SYS_ENOENT` at the caller |
| `open` of a missing file without `O_CREAT` | `-SYS_ENOENT` |
| `open` with `O_DIRECTORY` of a non-dir | `-SYS_ENOTDIR` |
| fd table full at `open` | file unref'd; `fd_alloc` error (`-SYS_EMFILE`) returned |
| Bad fd to any fd op | `-SYS_EBADF` |
| `read`/`write` on a directory | `-SYS_EISDIR` (via `vnode_read`/`vnode_write`) |
| `readdir` on a non-directory | `-SYS_ENOTDIR` |
| `lseek` to a negative offset / bad whence | `-SYS_EINVAL` |
| `write` to a ramfs file | `-SYS_EPERM` |
| `mkdir`/`create`/`unlink` on a read-only fs | `-SYS_EPERM` |
| `mkdir`/`unlink` of the root itself | `-SYS_EINVAL` (empty basename) |
| Path longer than the buffer / too many components | `-SYS_ENAMETOOLONG` |
| Boot: root ramfs or devfs fails to mount | `kernel_panic` |

## Verification

- **`make verify-vfs`** — boots the image and expects `[PASS] vfs_test` and
  `[PASS] fd_test` (covers path resolution, open/read/lseek/readdir, and the fd
  table). The `vfs_test` and `fd_test` programs live in the initramfs `/tests`.
- **`make verify-hnxfs`** — expects `[OK] HNXFS mounted at /disk` plus the HNXFS
  create/write/read/mkdir/unlink markers, which exercise the *writable* VFS mutation
  path (`vfs_mkdir`/`vfs_create`/`vfs_unlink` dispatching into a backend's
  `ops->create`/`ops->unlink`).
- **`make verify-expanded-userland`** — expects `[PASS] expanded coreutils` and
  `[PASS] storage user programs`, which run user tools (`ls`, `cat`, `pwd`,
  `mounts`, etc.) over the live namespace.
- **`make verify-tty`** — expects `[OK] TTY interactive input online`, exercising
  `/dev/console` through devfs.

Boot serial markers along the VFS path: `[OK] VFS online` (`vfs_init`),
`[OK] devfs online` (`devfs_create`), `[OK] File descriptor tables online`, and —
once disk storage is up — `[OK] HNXFS mounted at /disk`. The scripted boot shell in
`kernel_main` exercises the namespace with `ls /`, `ls /bin`, `cat /etc/banner.txt`,
`mounts`, and `ls /disk`.

## Future expansion

- **Unmount and remount.** `vfs_mount` has no inverse; the table never shrinks.
- **A dentry / vnode cache at the VFS layer.** Today each backend re-walks the path
  per resolve. A shared name cache would cut repeated lookups (HNXFS already caches
  vnodes by inode; ramfs/devfs do not).
- **Real `O_TRUNC` and access-mode enforcement.** `O_TRUNC` is a no-op and the
  `O_RDONLY`/`O_WRONLY` modes are not enforced in the VFS layer.
- **Mount-point nesting beyond prefixes.** The longest-prefix scheme works for the
  current flat layout; deeper graft points or bind mounts would need richer mount
  records.
- **`rename`, `link`, `symlink`.** `struct vnode_ops` currently has only
  `read`/`write`/`readdir`/`create`/`unlink`; there are no rename/link/symlink ops.
- **A writable ramfs (tmpfs).** ramfs is read-only; adding write/create/unlink would
  give a scratch in-memory filesystem.
- **More than 8 mounts.** `VFS_MAX_MOUNTS` is a fixed small array; a dynamic list
  would lift the cap.
