# Block Layer

The block layer is HNX/MyOS's sector-storage abstraction. It sits between the
storage controller drivers (AHCI, NVMe) and the filesystem layer (HNXFS), and it
provides three things:

1. A uniform `struct block_device` that any driver can fill in and register.
2. A small write-through sector cache keyed by `(device, lba)`.
3. A partition layer that parses MBR and GPT on whole disks and registers a child
   block device per partition (`disk0p1`, `disk0p2`, ...).

Everything in the block layer is grounded in `kernel/block/` and
`kernel/partition/`. It is deliberately minimal: synchronous, single-sector
cache lines, no async request queue yet. The data structures, however, are laid
out so the synchronous v0 can grow into an async, write-back design without
changing the driver-facing contract.

## Architecture

```
  filesystem (HNXFS) / userland (lsblk, dd)
                |
        block_read / block_write        kernel/block/block_device.c
                |
        block_cache_read / _write       kernel/block/block_cache.c  (64-line, direct-mapped, write-through)
                |
        dev->read / dev->write          driver op (AHCI disk_read/write, partition part_read/write)
                |
        AHCI port / partition parent
```

The contract a driver implements is `struct block_device`: a name, a geometry
(`sector_count`, `sector_size`), an opaque `driver_data` pointer, and two
function pointers `read` and `write` that move whole sectors at an LBA. The
driver is responsible for DMA; the buffer it is handed is a plain kernel buffer.

There are two cooperating registries:

- The **block registry** (`kernel/block/block_registry.c`) is a singly-linked
  list of `struct block_device`. It is the canonical list of block devices and
  is what `block_get_device("disk0p1")` searches.
- Every registration is **mirrored into the driver core** as a
  `DEV_TYPE_BLOCK` `struct device` so the generic `devices` enumerator can see
  block devices alongside PCI/char/input devices. See
  `block_register_device()` and the `device_register()` call it makes.

The cache is logically *above* the device but *below* the partition op:
`block_read` loops sector by sector through `block_cache_read`, which on a miss
calls `dev->read(dev, lba, ..., 1)`. For a partition device, `dev->read` is
`part_read`, which adds the partition's `lba_offset` and forwards to the parent
disk's raw `read` op. Because the cache key includes the device pointer, a
partition and its parent disk have independent cache lines even for overlapping
physical sectors.

### Boot wiring

From `kernel/src/kernel.c`, the Prompt 5 storage bring-up is:

```c
block_init();                 /* registry + cache; logs "[OK] Block layer online" */
ioapic_init();
ahci_init();                  /* registers the AHCI PCI driver */
nvme_init();                  /* registers the NVMe PCI driver  */
pci_driver_match_all();       /* probes AHCI (which registers disk0), NVMe foundation */

partition_init();             /* logs "[OK] Partition parser online" */
partition_scan_all();         /* parses MBR/GPT on every whole disk */

struct block_device *root_part = block_get_device("disk0p1");
/* ... hnxfs_mount(root_part), vfs_mount("/disk", ...) ... */
```

So the order is: registry+cache online, controller drivers register disks, then
the partition scan walks the disks and registers `diskNpM` children, then HNXFS
mounts from `disk0p1`.

## File map

| File | Role |
| --- | --- |
| `kernel/block/block_device.h` | `struct block_device`, `BLOCK_SECTOR_SIZE` (512), `BLOCK_NAME_MAX` (32); declares cached `block_read`/`block_write`. |
| `kernel/block/block_device.c` | `block_read`/`block_write`: sector-looping wrappers over the cache. |
| `kernel/block/block_cache.h` | Cache API + stats accessors (`block_cache_hits`, `_misses`, `_writes`, `_evictions`, `_dirty`). |
| `kernel/block/block_cache.c` | 64-line direct-mapped write-through cache; `slot_for()` hash; stats counters. |
| `kernel/block/block_registry.h` | Registry API: `block_init`, `block_register_device`, `block_get_device`, count/at iterators. |
| `kernel/block/block_registry.c` | Linked-list registry + driver-core mirroring; `block_init` brings up cache. |
| `kernel/block/block_request.h` | `struct block_request`, `enum block_op`, `block_request_submit`. |
| `kernel/block/block_request.c` | Synchronous request execution onto `block_read`/`block_write`. |
| `kernel/partition/partition.h` | `struct partition_info`; partition scan + register API. |
| `kernel/partition/partition.c` | `part_read`/`part_write` (offset forwarding), `partition_register`, `partition_scan_all`. |
| `kernel/partition/mbr.h` / `mbr.c` | Classic MBR parser; defers to GPT on a 0xEE protective entry. |
| `kernel/partition/gpt.h` / `gpt.c` | GPT header (LBA1) + entry-array parser. |
| `kernel/tests/storage_tests.c` | `storage_tests_run`: cache-hit, disk-read (MBR sig), partition, write round-trip. |

## Data structures

### `struct block_device` (`block_device.h`)

```c
#define BLOCK_SECTOR_SIZE 512u
#define BLOCK_NAME_MAX    32

struct block_device {
    char     name[BLOCK_NAME_MAX];     /* "disk0", "disk0p1" */
    uint64_t sector_count;
    uint32_t sector_size;              /* 512 */
    void    *driver_data;              /* struct ahci_port* / struct partition_info* */
    int (*read)(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count);
    int (*write)(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count);
    struct block_device *next;         /* registry link */
};
```

- `name` is fixed at 32 bytes. Disk names are `disk<N>` (AHCI); partition children
  append `p<index>` (so the name budget matters — see Invariants).
- `read`/`write` move `count` whole sectors; return 0 on success, negative on
  error. The `buffer` is a kernel buffer. The driver owns DMA.
- `driver_data` is the back-pointer to driver state: an `struct ahci_port *` for
  a real disk, a `struct partition_info *` for a partition child.

### `struct cache_line` (`block_cache.c`)

```c
#define CACHE_LINES 64

struct cache_line {
    struct block_device *dev;
    uint64_t lba;
    uint8_t  valid;
    uint8_t  dirty;                    /* always 0 in write-through */
    uint8_t  data[BLOCK_SECTOR_SIZE];  /* one 512-byte sector */
};
static struct cache_line g_lines[CACHE_LINES];
static uint64_t g_hits, g_misses, g_writes, g_evictions;
```

A line holds exactly one 512-byte sector. The cache is **direct-mapped**: the
slot for a `(dev, lba)` is fixed by a hash, so a new sector mapping to an
occupied slot evicts the prior occupant. The `dirty` field exists but is never
set: the design is write-through (writes hit the device first, then update the
clean cached copy). `dirty` and `block_cache_flush_all()` are placeholders for a
future write-back mode that arrives with journaling.

### `struct partition_info` (`partition.h`)

```c
struct partition_info {
    struct block_device *parent;   /* whole-disk device */
    uint64_t lba_offset;           /* start LBA on the parent */
    uint64_t lba_count;            /* size in sectors */
    uint8_t  type;                 /* MBR type byte, or 0x83 for GPT entries */
};
```

A partition child's `driver_data` points at one of these. `part_read`/
`part_write` use it to translate child LBAs to parent LBAs.

### `struct block_request` (`block_request.h`)

```c
enum block_op { BLOCK_OP_READ = 0, BLOCK_OP_WRITE = 1 };

struct block_request {
    struct block_device *dev;
    enum block_op        op;
    uint64_t             lba;
    uint32_t             count;
    void                *buffer;
    int                  status;   /* 0 = ok, negative = error */
};
```

In v0 a request is submitted synchronously. The struct is the foundation for a
later async queue (a driver could enqueue and complete out-of-line), but today
`block_request_submit` just dispatches inline and stores the result in `status`.

## Key APIs

### Cached transfers

```c
int block_read (struct block_device *dev, uint64_t lba, void *buffer, uint32_t count);
int block_write(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count);
```

These loop `count` times, one sector per iteration, routing each sector through
the cache (`block_cache_read`/`block_cache_write`). A NULL `dev` returns -1. Any
per-sector failure aborts with -1 (no rollback of already-transferred sectors).

### Cache (one sector at a time)

```c
void     block_cache_init(void);                                  /* logs "[OK] Block cache online" */
int      block_cache_read (struct block_device *dev, uint64_t lba, void *buffer);
int      block_cache_write(struct block_device *dev, uint64_t lba, const void *buffer);
void     block_cache_flush_all(void);                             /* no-op in write-through */
void     block_cache_dump_stats(void);
uint64_t block_cache_hits/misses/writes/evictions/dirty(void);
```

The slot function is:

```c
static struct cache_line *slot_for(struct block_device *dev, uint64_t lba) {
    uint64_t h = lba * 2654435761ull + ((uint64_t)(uintptr_t)dev >> 4);
    return &g_lines[h % CACHE_LINES];
}
```

`block_cache_read` semantics:
- Hit (`valid && dev matches && lba matches`): `g_hits++`, memcpy out, return 0.
- Miss: `g_misses++`; if the slot was a valid different mapping, `g_evictions++`.
  Call `dev->read(dev, lba, line->data, 1)`. On failure invalidate the line and
  return -1. On success record `(dev, lba)`, mark valid+clean, memcpy out.

`block_cache_write` is write-through:
- `g_writes++`, then call `dev->write(dev, lba, buffer, 1)` first. If it fails,
  return -1 and the cache is **not** updated.
- On success, populate the slot (counting an eviction if it displaced a different
  mapping), mark valid + `dirty = 0`, and memcpy the new data into the line.

### Registry

```c
void                 block_init(void);                              /* "[OK] Block layer online" */
int                  block_register_device(struct block_device *device);
struct block_device *block_get_device(const char *name);
void                 block_dump_devices(void);
int                  block_device_count(void);
struct block_device *block_device_at(int index);
```

`block_register_device`:
- Rejects NULL.
- Defaults `sector_size` to `BLOCK_SECTOR_SIZE` if the driver left it 0.
- Appends to the tail of the registry list (`g_head`/`g_tail`, `g_count++`).
- Mirrors a `DEV_TYPE_BLOCK` `struct device` into the driver core
  (`device_init_struct` + `device_register`), with `bus_data` pointing at the
  `block_device`. A `kcalloc` failure here is tolerated (the block device is
  still registered; only the driver-core mirror is skipped).
- Logs the device name and `sectors=<count>`.

### Request submission

```c
int block_request_submit(struct block_request *req);
```

Returns `-SYS_EINVAL` for a NULL request or NULL `req->dev`; otherwise dispatches
to `block_write`/`block_read` per `req->op`, stores the result in `req->status`,
and returns it.

### Partition layer

```c
void partition_init(void);          /* "[OK] Partition parser online" */
void partition_scan_all(void);      /* scan every whole disk */
int  mbr_parse(struct block_device *device);
int  gpt_parse(struct block_device *device);
void partition_dump_all(void);
int  partition_register(struct block_device *parent, int index,
                        uint64_t lba_offset, uint64_t lba_count, uint8_t type);
```

`partition_register` allocates a `block_device` + `partition_info`, builds the
`"<parent>pN"` name, sets `sector_count = lba_count`, inherits the parent's
`sector_size`, wires `part_read`/`part_write`, and registers the child.

`part_read`/`part_write` bound-check `lba + count > pi->lba_count` (return -1)
then forward to `pi->parent->read/write` at `pi->lba_offset + lba`. Note they
call the parent's **raw driver op directly**, not `block_read` — so a partition
transfer goes parent-raw, and the *partition's* cache lines are populated by the
cache layer sitting above `part_read` when callers use `block_read(partdev, ...)`.

## Partition parsing details

### MBR (`mbr.c`)

1. `block_read(device, 0, sector, 1)` — read LBA 0 through the cache.
2. Require the boot signature `sector[510]==0x55 && sector[511]==0xAA`, else -1.
3. For each of the 4 partition entries at offset `446 + i*16`:
   - `type = e[4]`, `start = rd32(&e[8])`, `count = rd32(&e[12])` (little-endian).
   - If `type == 0xEE` (GPT protective MBR), immediately `return gpt_parse(device)`.
   - Skip empty entries (`type == 0 || count == 0`).
   - `partition_register(device, i + 1, start, count, type)`; count successes.
4. Return 0 if at least one partition registered, else -1.

### GPT (`gpt.c`)

1. `block_read(device, 1, hdr, 1)` — read the GPT header at LBA 1.
2. Require the signature `memcmp(hdr, "EFI PART", 8) == 0`, else -1.
3. Read header fields (little-endian):
   - `entry_lba = rd64(&hdr[72])` — partition entry array start LBA.
   - `num_entries = rd32(&hdr[80])`.
   - `entry_size = rd32(&hdr[84])`; reject `< 128` or `> BLOCK_SECTOR_SIZE`.
4. Clamp `num_entries` to 128; `per_sector = BLOCK_SECTOR_SIZE / entry_size`.
5. Walk the entry array sector by sector. For each entry:
   - Skip if the 16-byte type GUID is all zero (unused).
   - `first = rd64(&e[32])`, `last = rd64(&e[40])`; skip if `last < first`.
   - `partition_register(device, idx + 1, first, last - first + 1, 0x83)`.
6. Return 0 if any registered, else -1.

GPT is tried first in `partition_scan_all` (`if (gpt_parse(...) != 0) mbr_parse(...)`),
and the MBR parser also forwards to GPT on a protective entry, so a GPT disk is
parsed once regardless of which path reaches it.

### `partition_scan_all`

Snapshots the whole-disk devices *before* adding children (so children added
mid-scan are not themselves scanned). A device is a "whole disk" if its name
contains no `'p'` (`!strchr(d->name, 'p')`). Up to 16 disks are scanned.

## Invariants

- **Sector size is 512.** `BLOCK_SECTOR_SIZE` is 512 throughout; the cache line
  is exactly 512 bytes; AHCI multiplies sector counts by 512. A device that left
  `sector_size == 0` is normalized to 512 at registration.
- **Cache key is `(dev, lba)`.** Two devices never share a cache line's identity,
  even when mapped to the same slot. A partition and its parent therefore have
  separate cached copies of overlapping physical sectors.
- **Write-through keeps the cache clean.** `dirty` is always 0; the device is the
  source of truth. A failed `dev->write` leaves the cache unchanged (no stale
  data is cached for a write that never reached the device).
- **The device write must succeed before the cache is updated.** Read-back
  consistency depends on this ordering in `block_cache_write`.
- **Whole-disk detection is name-based.** A disk name must not contain `'p'`; a
  partition name is `"<parent>pN"`. The disk naming scheme (`disk0`, `disk1`, ...)
  satisfies this because the base name has no `p`.
- **Partition index fits one digit.** `partition_register` writes the index as a
  single character `('0' + index)` and only does so if `n + 2 < sizeof(name)`.
  Indices above 9 produce non-digit name characters; names that would overflow
  the 32-byte buffer silently keep the parent name (the suffix is dropped).
- **Registry order is stable, append-only.** Devices are appended at the tail and
  never removed; `block_device_at(index)` is a stable position for the life of
  boot.

## Failure modes

- **NULL device →** `block_read`/`block_write` return -1; `block_request_submit`
  returns `-SYS_EINVAL`.
- **Driver op missing or failing →** `block_cache_read` invalidates the line and
  returns -1; `block_cache_write` returns -1 without touching the cache.
- **Partition out-of-range →** `part_read`/`part_write` return -1 when
  `lba + count > pi->lba_count`. This is a hard bound: a partition cannot read
  past its end into the next partition.
- **No valid partition table →** `mbr_parse`/`gpt_parse` return -1; the disk has
  no children. If `disk0p1` is consequently absent, `kernel.c` logs
  `hnxfs: disk0p1 not found` and `/disk` is not mounted.
- **`kcalloc` failure in `partition_register` →** returns -1; the partition is
  skipped, others continue.
- **PMM/heap exhaustion at registration →** the driver-core mirror is skipped
  (block device still usable); a fully failed `kcalloc` of the `block_device`
  itself (in AHCI/partition register paths) returns -1 up the stack.
- **More than 16 disks →** `partition_scan_all` only scans the first 16; extra
  whole disks are left unpartitioned.
- **More than 128 GPT entries →** clamped to 128; entries beyond are ignored.

## Verification

The block layer is exercised by three `make` targets, each booting the real
image under QEMU and grepping the serial log for markers emitted by code that
actually ran (`tools/verify_qemu.py --expect`).

```
make verify-pci       # PCI must be up first (storage rides on it)
make verify-block     # block layer + cache + partition parser
make verify-storage   # AHCI disk read/write round-trip
make verify-prompt5   # runs the whole Prompt 5 chain incl. the above
```

Serial markers and where they come from:

| Marker | Emitter |
| --- | --- |
| `[OK] Block layer online` | `block_init` (`block_registry.c`) |
| `[OK] Block cache online` | `block_cache_init` (`block_cache.c`) |
| `[OK] Partition parser online` | `partition_init` (`partition.c`) |
| `[PASS] block cache` | `storage_tests_run`: re-read of LBA 0 increments `block_cache_hits()` |
| `[PASS] disk read` | `storage_tests_run`: LBA 0 returns `0x55 0xAA` MBR signature |
| `[PASS] partition parser` | `storage_tests_run`: `block_get_device("disk0p1")` non-NULL |
| `[PASS] disk write` | `storage_tests_run`: raw write+read round-trip on `disk0p2` matches |

`verify-block` expects `[OK] Block layer online`, `[PASS] block cache`, and
`[PASS] partition parser`. `verify-storage` expects `[OK] AHCI block device
online`, `[PASS] disk read`, `[PASS] disk write`. The `disk write` test
deliberately bypasses the cache by calling `scratch->write`/`scratch->read`
(the raw ops) so the round-trip really hits the backing store.

The expanded suite (`Makefile.production`) adds `verify-block-expanded` and
`verify-ahci-expanded` to `verify-production-200k`.

## Future expansion

The structures are intentionally sized for growth; concrete next steps the code
already gestures at:

- **Write-back caching.** `dirty`, `block_cache_flush_all`, and the
  `block_cache_dirty()` counter exist but do nothing today. A write-back mode
  would set `dirty` on `block_cache_write`, skip the immediate device write, and
  flush on `flush_all`/eviction. The comment in `block_cache.c` ties this to the
  arrival of journaling.
- **Async request queue.** `struct block_request` + `block_request_submit` are
  the seam. Today submission is inline; a real queue would let drivers complete
  out of line (e.g. on an AHCI/NVMe completion interrupt) and call back into the
  request's `status`.
- **Set-associative or larger cache.** `CACHE_LINES` (64) and the direct-mapped
  `slot_for` hash could become N-way to reduce eviction thrash on hot disks.
- **Multi-sector cache lines / clustered I/O.** Today each cache line is one
  sector and `block_read` loops per sector. A clustered design would fetch and
  cache runs of sectors per `dev->read` call.
- **Partition index beyond 9 and >16 disks.** The one-digit index and 16-disk
  scan cap are simple limits that a multi-digit name formatter and a dynamic disk
  list would lift.
- **NVMe block devices.** NVMe currently registers no block device (see
  `docs/nvme.md`); once its namespace I/O lands it will register `nvmeN`-style
  block devices through the same `block_register_device` path, and the partition
  scanner will pick them up automatically.
