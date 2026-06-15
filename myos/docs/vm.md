# Virtual memory region tracker

The VM region tracker (`kernel/memory/vmregion.c`) is the bookkeeping layer that
records what an address space *intends* to contain — an ordered set of
non-overlapping `[start, end)` regions, each with protection bits and flags. It is
distinct from, and sits above, the hardware page tables managed by the VMM
(`kernel/memory/vmm.c`) and the physical page allocator (`kernel/memory/pmm.c`).

This is the foundation for `mmap`/`munmap`, demand paging, copy-on-write, and page
fault accounting: the region tracker answers "is this address part of a mapping,
and what are its permissions?" while the VMM answers "is this address actually
mapped in hardware, and to which physical page?".

## Architecture

```
   syscall / fault handler
        |
        |  vm_map_insert / find / remove / protect / mark_cow / account_fault
        v
   +-------------------------------+        +---------------------------+
   | vm_map  (per address space)   |        | vmm  (page tables, CR3)   |
   |  ordered list of vm_region    |        |  vmm_map_page / unmap     |
   +---------------+---------------+        +-------------+-------------+
                   | region storage from                 | frames from
                   v slab (kmem_cache "vm_region")        v
              +--------------------------------------------------+
              | pmm  (pmm_alloc_page / pmm_alloc_contig, bitmap) |
              +--------------------------------------------------+
```

The region tracker is intentionally decoupled from the page tables:

- `vm_map`/`vm_region` describe **intent** (an mmap'd range, its prot/flags).
- `vmm_map_page`/`vmm_unmap_page`/`vmm_get_physical` realize **hardware** mappings.
- `pmm` supplies the physical frames.

A real fault handler would consult `vm_map_find` to validate an access, then call
the VMM to install (or copy, for COW) the page. The tracker also keeps fault
*counters* so the address space can be characterized (minor/major/cow/protection).

Region control blocks are allocated from a dedicated slab cache, `"vm_region"`,
created lazily on first insert with `KMEM_CACHE_ZERO` so a fresh region starts
clean.

### Relationship to the existing pmm/vmm

The three layers map cleanly onto the three questions an address space asks:

| Question | Layer | Concrete call |
|----------|-------|---------------|
| "Is `addr` part of a mapping, and with what permissions?" | region tracker | `vm_map_find(m, addr)` |
| "Is `addr` mapped in hardware, and to which frame?" | VMM | `vmm_get_physical(addr)` |
| "Give me a physical frame to back it." | PMM | `pmm_alloc_page()` / `pmm_alloc_contig(n)` |

The VMM (`vmm.c`) builds the kernel page tables at boot: it identity-maps all
physical RAM plus the framebuffer with 2 MiB large pages, then maps the kernel
image into the higher half, and finally loads CR3. Its public surface
(`vmm_map_page`, `vmm_unmap_page`, `vmm_get_physical`, `vmm_map_mmio_2m`) is what a
fault handler would call to *realize* a region's intent in hardware. The region
tracker never calls these itself — it is pure bookkeeping — which keeps it testable
without a live CR3 and lets the same `vm_map` logic describe both kernel and (future)
user address spaces. The `memory_layout.h` constants (`PAGE_SIZE`, `PAGE_MASK`,
`PAGE_ALIGN_UP`) are shared by all three layers so alignment is defined in one
place.

## File map

| File | Purpose |
|------|---------|
| `kernel/memory/vmregion.h` | `vm_map`, `vm_region`, prot/flag bit definitions, full API |
| `kernel/memory/vmregion.c` | Region tracker impl: insert/find/overlap/find_free/remove/protect/mark_cow/account_fault/dump |
| `kernel/memory/memory_layout.h` | `PAGE_SIZE`, `PAGE_MASK`, `PAGE_ALIGN_UP`, address-space constants |
| `kernel/memory/vmm.h` / `vmm.c` | Hardware page tables (identity + higher half), `vmm_map_page`, MMIO remap |
| `kernel/memory/pmm.h` / `pmm.c` | Physical frame allocator the VMM/slab draw from |
| `kernel/memory/slab.h` | `kmem_cache_create`/`alloc`/`free` used for `vm_region` storage |
| `kernel/tests/vm_tests.c` | Boot self-test for the tracker (marker `VM region tests`) |

## Data structures

### `struct vm_region` (`vmregion.h`)

```c
struct vm_region {
    struct list_node link;
    uint64_t   start;       /* inclusive, page-aligned */
    uint64_t   end;         /* exclusive, page-aligned */
    uint32_t   prot;        /* VM_PROT_READ|WRITE|EXEC */
    uint32_t   flags;       /* VM_FLAG_* */
    const char *name;
    uint64_t   fault_count; /* page faults serviced in this region */
};
```

One contiguous mapping. `link` threads it onto its `vm_map`'s ordered list.
`fault_count` is a per-region fault tally (separate from the map-wide counters).

### `struct vm_map` (`vmregion.h`)

```c
struct vm_map {
    struct list_node regions;   /* ordered ascending by start          */
    uint64_t   base, limit;     /* [base, limit) mappable window        */
    size_t     region_count;
    uint64_t   total_mapped;    /* sum of region sizes, bytes           */
    const char *name;
    /* fault accounting */
    uint64_t   minor_faults, major_faults, cow_faults, protection_faults;
};
```

An entire address space's region set. `base`/`limit` bound where regions may live.
`total_mapped` is maintained incrementally on every insert/remove/split. The four
fault counters classify faults as they are accounted.

### Protection bits (`vmregion.h`)

| Constant | Value | Meaning |
|----------|-------|---------|
| `VM_PROT_READ` | `0x1` | readable |
| `VM_PROT_WRITE` | `0x2` | writable |
| `VM_PROT_EXEC` | `0x4` | executable |

### Region flags (`vmregion.h`)

| Constant | Value | Meaning |
|----------|-------|---------|
| `VM_FLAG_USER` | `0x01` | user-accessible |
| `VM_FLAG_ANON` | `0x02` | anonymous (zero-fill) |
| `VM_FLAG_FILE` | `0x04` | file-backed |
| `VM_FLAG_COW` | `0x08` | copy-on-write |
| `VM_FLAG_GROWSDOWN` | `0x10` | stack-like |
| `VM_FLAG_FIXED` | `0x20` | placed at an exact address |
| `VM_FLAG_SHARED` | `0x40` | shared mapping |

## Key APIs

```c
void vm_map_init(m, base, limit, name);
void vm_map_destroy(m);

int  vm_map_insert(m, start, end, prot, flags, name);   /* 0 / -1 */
struct vm_region *vm_map_find(m, addr);                  /* containing region or NULL */
int  vm_map_overlaps(m, start, end);                    /* 1 if any overlap */
uint64_t vm_map_find_free(m, hint, size);              /* gap start or 0 */
uint64_t vm_map_remove(m, start, end);                 /* bytes unmapped */
int  vm_map_protect(m, start, end, prot);              /* 0 / -1 */
int  vm_map_mark_cow(m, start, end);                   /* regions touched */
struct vm_region *vm_map_account_fault(m, addr, write, present);
void vm_map_dump(m);
```

### `vm_map_insert`

Validates alignment and range, rejects overlaps, then inserts in ascending order.
The validity gate is `aligned_range`: `(start & PAGE_MASK) == 0 &&
(end & PAGE_MASK) == 0 && start < end`. It also requires `start >= base` and
`end <= limit`, and `!vm_map_overlaps(m, start, end)`. On success it allocates a
`vm_region` from the slab, fills it, finds the first existing region with a greater
`start`, and links the new region before it (via `__list_add` against `pos->prev`),
appending at the tail if none is greater. Updates `region_count` and
`total_mapped`. Returns `-1` on misalignment, out-of-range, overlap, or allocation
failure. Insert is `O(region_count)` (the ordered-position scan).

### `vm_map_find`

Walks the ordered list returning the region with `addr >= start && addr < end`. The
ordering lets it stop early: once a region's `start > addr`, no later region can
contain `addr`, so it breaks. `O(region_count)` worst case, often much less.

### `vm_map_overlaps`

Returns 1 if `[start, end)` intersects any region, using the standard interval test
`start < r->end && r->start < end`. `O(region_count)`.

### `vm_map_find_free`

Finds the lowest page-aligned gap of `size` bytes at or above `hint`. It rounds
`size` up (`PAGE_ALIGN_UP`), starts a `cursor` at `max(base, align_up(hint))`, and
walks the ordered regions: regions entirely below the cursor are skipped; if the
next region starts at least `cursor + size` away the gap `[cursor, r->start)` fits
and `cursor` is returned; otherwise `cursor` jumps past the overlapping region
(`cursor = r->end`). After the loop, if `cursor + size <= limit` it returns
`cursor`, else `0` (no space). `O(region_count)`.

### `vm_map_remove` (unmap / split / punch-hole)

Removes `[start, end)`, adjusting every overlapping region, and returns the total
bytes unmapped. For each overlapping region it computes the overlap
`[max(start,r->start), min(end,r->end))`, adds its length to `removed`, and applies
one of four cases:

1. **Whole region covered** (`start <= r->start && end >= r->end`): delete the
   region, `region_count--`, subtract its size from `total_mapped`.
2. **Hole strictly inside** (`start > r->start && end < r->end`): shrink `r` to the
   left part (`r->end = start`) and `vm_map_insert` a new right part
   `[end, old_end)` inheriting `prot`/`flags`/`name` — this is the split that
   *raises* `region_count`.
3. **Front trim** (`start <= r->start`): `r->start = end`.
4. **Tail trim** (otherwise): `r->end = start`.

Iterates with `list_for_each_safe` so a region freed mid-walk is safe. Requires an
aligned range (returns 0 otherwise). `O(region_count)`.

### `vm_map_protect`

Sets `prot` on every region overlapping `[start, end)`. For simplicity protection
applies to *whole* overlapped regions (it does not split at the boundary). Returns
0 if at least one region was touched, `-1` if none (or the range is misaligned).

### `vm_map_mark_cow`

Marks overlapping regions copy-on-write: for each writable region
(`prot & VM_PROT_WRITE`) it sets `VM_FLAG_COW`. The intent is that those pages
become read-only in hardware until a write fault copies them. Returns the number of
regions touched. Note it sets the flag but does not clear `VM_PROT_WRITE` in the
region record — the read-only enforcement happens at the page-table level.

### `vm_map_account_fault`

Classifies and counts a fault at `addr`. It looks up the region:

- **No region** -> `major_faults++`, return NULL (fault in an unmapped gap).
- Region found -> `r->fault_count++`, then exactly one of:
  - `write && (flags & VM_FLAG_COW)` -> `cow_faults++`
  - `write && !(prot & VM_PROT_WRITE)` -> `protection_faults++`
  - `present` -> `minor_faults++`
  - else -> `major_faults++`

Returns the region (or NULL). This is the accounting hook a real `#PF` handler would
call before servicing the fault.

### `vm_map_dump`

Logs a one-line summary (`regions`, `mapped KiB`, and the four fault counters) then
one line per region with `rwx` prot characters, hex flags, and the name.

### Worked example: the punch-hole split

The most subtle operation is removing a range that lies strictly inside a single
region, which turns one region into two. Starting from one anonymous RW region:

```
before:   [0x20000000 ............................. 0x20100000)   prot=rw- ANON "data"
                       region_count = 1,  total_mapped = 0x100000 (1 MiB)

vm_map_remove(m, 0x20040000, 0x20080000):
  overlap = [0x20040000, 0x20080000)            removed += 0x40000 (256 KiB)
  case 2 (hole strictly inside):
     - shrink original to the left part:  r->end = 0x20040000
     - insert right part [0x20080000, 0x20100000) inheriting prot/flags/name

after:    [0x20000000 . 0x20040000)   gap   [0x20080000 . 0x20100000)
           prot=rw- "data"                    prot=rw- "data"
                       region_count = 2 (split raised it),  removed returned = 0x40000
```

The left fragment is shrunk in place (no allocation); the right fragment is a fresh
`vm_region` from the slab. `total_mapped` is adjusted by subtracting the original
size and re-adding the shrunk left size, with the `vm_map_insert` of the right part
adding its own size — so the net effect is exactly `-0x40000`. This is the path the
self-test verifies with `removed == 0x40000` and `region_count == 3` (the test had
two regions before the split).

## Invariants

- **Ordering:** the region list is strictly ascending by `start`; `vm_map_insert`
  maintains it and `vm_map_find`/`find_free` rely on it for early exit.
- **Non-overlap:** no two regions overlap; enforced by the `vm_map_overlaps` check
  in `insert` (and preserved by `remove`, which only shrinks/splits).
- **Alignment:** every region's `start` and `end` are page-aligned and
  `start < end` (`aligned_range`). Inserts/removes/protects with a misaligned range
  are rejected.
- **Window:** every region satisfies `base <= start` and `end <= limit`.
- **Accounting:** `total_mapped` equals the sum of `(end - start)` over all
  regions; `region_count` equals the number of list entries. Both are updated on
  every mutation, including the split path.
- **Fault classification is mutually exclusive:** each `account_fault` call
  increments exactly one of the four map counters.
- **COW semantics:** `VM_FLAG_COW` is only set on regions that were writable; a
  subsequent write to such a region is counted as a COW fault, not a protection
  fault.

## Failure modes

- **Overlap rejected.** Inserting a range that intersects an existing region
  returns `-1`; the caller must pick a different placement (often via
  `vm_map_find_free`).
- **Out-of-range / misaligned rejected.** `insert` returns `-1` for ranges outside
  `[base, limit)` or not page-aligned; `remove`/`protect` return `0`/`-1` for
  misaligned ranges.
- **No free gap.** `vm_map_find_free` returns `0` when no gap of the requested size
  exists below `limit`. `0` is unambiguous because `base` is the lowest mappable
  address and never the result.
- **Allocation failure.** If the `vm_region` slab cache cannot be created or grown,
  `region_alloc` returns NULL and `insert` returns `-1`. A split inside `remove`
  calls `vm_map_insert` for the right part; if that allocation fails, the right part
  is silently lost (the left part is still correctly shrunk) — a known limitation of
  the punch-hole path under memory pressure.
- **Protection is region-granular.** `vm_map_protect`/`vm_map_mark_cow` apply to
  whole overlapped regions; a sub-region protection change is not split out, so
  asking to protect a partial range changes the entire enclosing region.
- **Tracker vs. hardware drift.** The tracker does not touch page tables; if a
  caller updates regions without making the matching `vmm_map_page`/`unmap_page`
  calls, the intent and the hardware diverge. Keeping them in sync is the fault
  handler's / mmap layer's responsibility.

## Verification

The tracker is exercised at boot by `vm_tests_run()` in `kernel/tests/vm_tests.c`,
emitting `[PASS] VM region tests`, gated by `make verify-kernel-core-expanded`
(and `make verify-production-200k`).

What the test asserts, in order:

- Empty map after `vm_map_init(&m, 0x10000000, 0x80000000, "test")`.
- Two successful inserts (`data` anon RW, `text` file RX) -> `region_count == 2`.
- Rejection of an overlapping insert, an out-of-range insert
  (`0x90000000 > limit`), and a misaligned insert (`0x40000001`).
- `vm_map_find` inside `data` returns the region with `VM_FLAG_ANON`; a find in a
  gap returns NULL.
- `vm_map_find_free(0x10000000, 1 MiB)` returns a non-zero gap that does not
  overlap.
- `vm_map_protect(text, READ)` succeeds and the region's `prot == VM_PROT_READ`.
- `vm_map_mark_cow(data)` touches exactly one region and sets `VM_FLAG_COW`.
- **Punch-hole split:** `vm_map_remove(0x20040000, 0x20080000)` returns `0x40000`
  (256 KiB) and raises `region_count` to 3; the hole is unmapped while the left and
  right fragments remain mapped.
- **Fault accounting:** a minor read fault, a write to the COW region, and a fault
  in a gap leave `cow_faults >= 1` and `major_faults >= 1`.
- `vm_map_destroy` returns `region_count` to 0.

## Future expansion

- **Boundary-precise protect.** Split regions at `[start, end)` boundaries in
  `vm_map_protect`/`vm_map_mark_cow` so partial-range permission changes do not
  affect the whole enclosing region.
- **Faster lookup.** Replace the ordered list with a balanced tree or augmented
  interval tree so `find`/`overlaps`/`find_free` become `O(log n)` for address
  spaces with many regions.
- **Region merging (coalescing).** After `remove` or `protect`, merge adjacent
  regions with identical `prot`/`flags`/`name`/backing to keep `region_count` low.
- **Real fault servicing.** Wire `vm_map_account_fault` into the `#PF` handler so it
  drives demand paging (anonymous zero-fill via `pmm_alloc_page` + `vmm_map_page`)
  and COW copy-on-write (copy frame, remap writable, clear `VM_FLAG_COW`).
- **File backing.** Use `VM_FLAG_FILE` to record an inode + offset so a file-backed
  fault can pull the page from the page cache.
- **Per-process maps.** Instantiate a `vm_map` per process address space (with
  `VM_FLAG_USER` regions), feeding `mmap`/`munmap`/`mprotect` syscalls directly into
  these APIs.
