# Memory allocators

The kernel runs three cooperating allocators, layered from oldest/simplest to the
production allocator introduced in Work Unit A:

1. **Legacy bump heap** (`kmalloc`/`kcalloc`/`kfree`) — a static `.bss` arena that
   never reclaims. Used by early code and by data structures created before the
   slab allocator exists.
2. **Slab object caches** (`kmem_cache_*`) — per-object-size caches that recycle
   fixed-size slots, backed by physically-contiguous page runs.
3. **Size-class general allocator** (`kmem_alloc`/`kmem_free`/`kmem_realloc`) — a
   `malloc`-style allocator routed through a fixed set of power-of-two caches, with
   a large-object fallback straight to the page allocator.

All three sit on top of the physical memory manager (`pmm`) and the identity map,
which is what makes the returned physical addresses usable as pointers directly.

## Architecture

```
   callers
     |  kmem_alloc/free/realloc        kmem_cache_alloc/free        kmalloc/kfree
     v                                       v                          v
  +-----------------------------+   +---------------------+    +-----------------+
  | size-class general allocator|-->|   kmem_cache        |    | bump heap       |
  | (g_size_caches[NUM_CLASSES])|   | (slab object cache) |    | (2 MiB .bss)    |
  +--------------+--------------+   +----------+----------+    +-----------------+
                 | large (> 4096)             | cache_grow
                 v                            v
            +--------------------------------------+
            | pmm_alloc_contig(n)  (identity-mapped)|
            +--------------------------------------+
```

Key relationship: `pmm_alloc_contig(n)` returns a base **physical** address for a
run of `n` 4 KiB pages, and because `vmm_init` identity-maps all RAM with 2 MiB
pages (`physical == virtual` for low memory), that physical address is also a valid
kernel pointer. The slab layer therefore casts it straight to `uint8_t *` and
carves objects out of it — no separate VMM mapping per slab.

Bring-up order (`kernel/src/kernel.c`):

```
heap_init();        /* bump arena reset                     */
...
kernel_core_init(); /* calls slab_init() -> builds the size-class caches */
```

`heap_init` must run first because the slab layer's `kmem_cache` *control blocks*
(`struct kmem_cache`) are themselves allocated with `kmalloc` (the bump heap),
while the slab *object storage* comes from `pmm_alloc_contig`.

## File map

| File | Purpose |
|------|---------|
| `kernel/memory/heap.h` | Bump heap API (`heap_init`, `kmalloc`, `kcalloc`, `kfree`, `heap_dump_stats`) |
| `kernel/memory/heap.c` | 2 MiB `.bss` arena, bump pointer, no-op `kfree`, panic-on-OOM |
| `kernel/memory/slab.h` | `kmem_cache`, `kmem_stats`, cache + general allocator + diagnostics APIs |
| `kernel/memory/slab.c` | Slab cache impl, size-class allocator, allocation headers, redzone, stats, leak dump |
| `kernel/memory/pmm.h` | Page allocator API including `pmm_alloc_contig`/`pmm_free_contig` |
| `kernel/memory/pmm.c` | Bitmap page allocator; `pmm_alloc_contig` first-fit run finder |
| `kernel/memory/memory_layout.h` | `PAGE_SIZE`, alignment macros, identity/higher-half constants |
| `kernel/tests/allocator_tests.c` | Self-tests for `kmem_alloc/free/realloc` (marker `kmalloc/kfree tests`) |
| `kernel/tests/slab_tests.c` | Self-tests for `kmem_cache_*` incl. redzone (marker `slab allocator tests`) |

## Data structures

### Bump heap (`heap.c`)

No public struct. The arena is module-private:

```c
#define HEAP_ARENA_SIZE (2 * 1024 * 1024)   /* 2 MiB */
#define HEAP_ALIGN      16
static uint8_t  g_arena[HEAP_ARENA_SIZE] __attribute__((aligned(16)));
static uint64_t g_offset;        /* next free byte */
static uint64_t g_alloc_count;   /* statistics */
```

The arena lives in `.bss`, so it is part of the kernel image (identity-mapped and
higher-half mapped) and needs no VMM setup.

### `struct kmem_cache` (`slab.h`)

```c
struct kmem_cache {
    const char       *name;
    size_t            obj_size;   /* requested object size                 */
    size_t            slot_size;  /* aligned slot incl. bookkeeping/redzone*/
    size_t            align;
    unsigned          flags;      /* KMEM_CACHE_REDZONE | KMEM_CACHE_ZERO  */
    void             *free_list;  /* singly-linked free slots              */
    struct kmem_slab *slabs;      /* all slabs owned by this cache         */
    size_t            objs_per_slab, slab_pages;
    /* statistics */
    uint64_t          alloc_count, free_count, active, slab_count, grow_count;
    struct kmem_cache *next;      /* global cache registry link            */
};
```

`active == alloc_count - free_count` is the live-object count (the leak metric).
`free_list` threads a next-pointer through each free slot's own storage.

### `struct kmem_slab` (`slab.c`, private)

```c
struct kmem_slab {
    struct kmem_cache *cache;
    struct kmem_slab  *next;
    uint8_t           *base;   /* first object                 */
    size_t             pages, nobjs, inuse;
};
```

A slab is a contiguous page run laid out as
`[struct kmem_slab][obj][obj]...[obj]`. The header sits at the start of the run;
objects begin at `base = mem + sizeof(struct kmem_slab)`.

### `struct free_slot` (`slab.c`, private)

```c
struct free_slot { struct free_slot *next; };
```

The free-list node is overlaid on a free object's own bytes — which is why
`cache_slot_size` forces `slot_size >= sizeof(struct free_slot)`.

### `struct kmem_hdr` — size-class allocation header (`slab.c`)

```c
#define KMEM_HDR_SIZE 16u
#define KMEM_MAGIC    0x4B4D454Du   /* 'KMEM' */
struct kmem_hdr {
    uint32_t magic;      /* KMEM_MAGIC; poisoned to 0 on free        */
    uint16_t class_idx;  /* 0..NUM_CLASSES-1, or 0xFFFF for "large"  */
    uint16_t flags;
    uint64_t npages;     /* page count for large allocations         */
};
```

Every `kmem_alloc` result is preceded by a 16-byte header. The returned pointer is
`hdr + KMEM_HDR_SIZE`; `kmem_free`/`kmem_alloc_size` recover the header by
subtracting 16. `magic` doubles as a double-free / corruption guard.

### Size classes (`slab.c`)

```c
static const size_t g_class_usable[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
#define NUM_CLASSES 9
static struct kmem_cache *g_size_caches[NUM_CLASSES];
```

Each class is a `kmem_cache` whose object size is `usable + KMEM_HDR_SIZE`,
16-byte aligned. Requests larger than 4096 bytes (usable) take the large path.

### `struct kmem_stats` (`slab.h`)

```c
struct kmem_stats {
    uint64_t total_allocs, total_frees, live_objects, bytes_live, large_allocs, cache_count;
};
```

`kmem_get_stats` fills it from the module globals `g_total_allocs`,
`g_total_frees`, `g_bytes_live`, `g_large_allocs`, plus a walk of the cache
registry for `cache_count`.

## Key APIs

### Bump heap

```c
void  heap_init(void);                /* g_offset = 0                          */
void *kmalloc(uint64_t size);         /* bump; PANIC on arena exhaustion       */
void *kcalloc(uint64_t count, size_t);/* kmalloc + zero                        */
void  kfree(void *ptr);               /* NO-OP (documented)                    */
void  heap_dump_stats(void);
```

`kmalloc` aligns `g_offset` up to 16, checks `start + size <= HEAP_ARENA_SIZE`, and
on overflow calls `kernel_panic_hex("heap: out of memory, requested", size)`. There
is no recovery: the bump heap is for boot-time allocations that live forever.
`kfree` is intentionally a no-op — freed bump memory is never reclaimed.

### Slab object caches

```c
struct kmem_cache *kmem_cache_create(name, obj_size, align, flags);
void  *kmem_cache_alloc(c);   /* pops free_list; grows the cache if empty      */
void   kmem_cache_free(c, obj);/* pushes onto free_list; validates redzone     */
void   kmem_cache_destroy(c); /* frees every slab to the PMM, unlinks, kfrees c*/
size_t kmem_cache_active(c);
```

`kmem_cache_create` computes `slot_size` via `cache_slot_size` (object size, plus
8 bytes if `KMEM_CACHE_REDZONE`, raised to at least `sizeof(struct free_slot)`, then
aligned up). It sizes a slab to hold at least `SLAB_MIN_OBJS == 8` objects:
`slab_pages = ceil((sizeof(kmem_slab) + slot_size*8) / PAGE_SIZE)`, and
`objs_per_slab = (slab_pages*PAGE_SIZE - sizeof(kmem_slab)) / slot_size`. The new
cache is pushed onto the global `g_cache_registry`.

`kmem_cache_alloc` is `O(1)`: if `free_list` is empty it calls `cache_grow` (one
`pmm_alloc_contig(slab_pages)`), threads every object onto the free list, then pops
the head. With `KMEM_CACHE_ZERO` it `memset`s the object; with `KMEM_CACHE_REDZONE`
it writes the trailing canary. `kmem_cache_free` validates the canary
(`check_redzone`) and pushes the slot back — also `O(1)`.

#### Worked example: sizing a cache

Take the size-class cache for 64-byte allocations, created by `slab_init` as
`kmem_cache_create("kmem-64", 64 + 16, 16, 0)` (object size = usable 64 + the
16-byte `kmem_hdr`):

```
obj_size  = 80
slot_size = cache_slot_size(80, 16, 0)
          = align_up(max(80, sizeof(free_slot)), 16) = align_up(80, 16) = 80
want      = sizeof(kmem_slab) + slot_size * SLAB_MIN_OBJS  (SLAB_MIN_OBJS = 8)
          = sizeof(kmem_slab) + 80*8                       (~640 + header)
slab_pages   = ceil(want / 4096) = 1
usable       = 1*4096 - sizeof(kmem_slab)
objs_per_slab = usable / 80         (~50 objects per 4 KiB slab)
```

So one `cache_grow` (a single `pmm_alloc_contig(1)`) yields ~50 reusable 80-byte
slots. The first `kmem_cache_alloc` on an empty cache pays for the page; the next
~49 are pure free-list pops. The redzone variant would make `slot_size` 88
(`80 + 8` canary, aligned to 16 -> 96), reducing `objs_per_slab` accordingly.

### Size-class general allocator

```c
void  slab_init(void);                 /* build g_size_caches[0..8]            */
void *kmem_alloc(size_t size);
void *kmem_zalloc(size_t size);        /* kmem_alloc + memset 0                */
void *kmem_realloc(void *ptr, size_t);
void  kmem_free(void *ptr);
size_t kmem_alloc_size(const void *ptr);
```

`kmem_alloc` maps `size` to the smallest class whose `usable >= size`
(`size_to_class`, a linear scan over 9 entries). For a class hit it
`kmem_cache_alloc`s a slot, stamps a `kmem_hdr` (`magic`, `class_idx`), and returns
`slot + 16`. For a miss (size > 4096) it takes the **large path**: round
`size + 16` up to whole pages, `pmm_alloc_contig`, stamp the header with
`class_idx = 0xFFFF` and `npages`, return `hdr + 16`.

`kmem_free` recovers the header, checks `magic == KMEM_MAGIC` (logs
`kmem_free: bad/double free (magic mismatch)` and returns if not), poisons
`magic = 0`, and routes to either `pmm_free_contig` (large) or `kmem_cache_free`
(class). `kmem_realloc` shrinks in place when `kmem_alloc_size(ptr) >= new_size`,
otherwise allocates, `memcpy`s the old usable bytes, and frees the old block.
`kmem_alloc_size` returns the class's usable bytes, or
`npages*PAGE_SIZE - KMEM_HDR_SIZE` for a large block.

### Diagnostics

```c
void kmem_get_stats(struct kmem_stats *out);
void kmem_dump_caches(void);   /* one line per cache: obj/slot/active/slabs     */
void kmem_leak_dump(void);     /* "LEAK <cache>: N objects still active" or none*/
```

`kmem_leak_dump` walks the cache registry and prints any cache with `active != 0`;
if none it prints `(no cache leaks)`.

### Underlying page allocator

```c
uint64_t pmm_alloc_contig(uint64_t count);  /* base phys addr or PMM_INVALID_PAGE */
void     pmm_free_contig(uint64_t phys, uint64_t count);
```

`pmm_alloc_contig` is a first-fit run finder over the page bitmap: starting just
above `LOW_MEMORY_RESERVED_END` (1 MiB), it scans for `count` consecutive free
pages, sets them, decrements the free count, and returns `page << PAGE_SHIFT`. A
single page short-circuits to `pmm_alloc_page`. On no run it returns
`PMM_INVALID_PAGE` (0). `pmm_free_contig` just frees each page in turn.

## Invariants

- **Bump heap:** `g_offset` only ever increases (until `heap_init` resets it); a
  returned pointer is 16-byte aligned and within `g_arena`.
- **Slab cache:** `active == alloc_count - free_count`; `slot_size >=
  sizeof(struct free_slot)` and `slot_size >= obj_size (+8 if redzone)`, aligned to
  `align`; every slab holds `objs_per_slab` slots; the `free_list` only contains
  slots not currently handed out.
- **Size class:** `g_class_usable` is strictly ascending; `size_to_class` returns
  the *smallest* fitting class; a class allocation's usable size is exactly
  `g_class_usable[class_idx]`.
- **Header:** a live `kmem_alloc` block has `hdr.magic == KMEM_MAGIC`; a freed
  block has `magic == 0` (poison), making double-free detectable.
- **Returned pointer identity:** because slab storage is identity-mapped,
  `(uintptr_t)ptr` equals the physical address of its header plus 16.
- **Reuse:** freeing a class object and immediately re-allocating the same class
  returns the same address (LIFO free list) — asserted by the tests.
- **Stats:** `live_objects == total_allocs - total_frees`; `bytes_live` tracks the
  sum of usable sizes of live allocations (class usable or `npages*PAGE_SIZE`).

## Failure modes

- **Bump heap exhaustion is fatal.** `kmalloc` past the 2 MiB arena calls
  `kernel_panic_hex` — there is no graceful failure. This caps how much can be
  allocated before `slab_init` makes the reclaiming allocator available.
- **`kfree` does nothing.** Memory freed via the bump heap is never reclaimed; long
  boot paths that churn `kmalloc` will leak into the arena. Prefer `kmem_alloc` for
  anything created after `slab_init`.
- **PMM out of contiguous pages.** `cache_grow` and the large `kmem_alloc` path
  return failure when `pmm_alloc_contig` yields `PMM_INVALID_PAGE`; `kmem_cache_alloc`
  and `kmem_alloc` then return NULL (callers must check). Contiguous runs can be
  unavailable even when total free memory is high, due to fragmentation (first-fit).
- **Double free / bad pointer.** `kmem_free` on a block whose `magic` is not
  `KMEM_MAGIC` logs `kmem_free: bad/double free (magic mismatch)` and returns
  without freeing (avoids corrupting the free list). Passing a pointer not produced
  by `kmem_alloc` is undefined beyond the magic check.
- **Redzone corruption.** If a `KMEM_CACHE_REDZONE` object is overwritten past
  `obj_size`, `kmem_cache_free`'s `check_redzone` fails and logs
  `slab: redzone corruption detected on free` (then still frees the slot). Redzone
  mode adds 8 bytes per slot and is a debug aid, not on by default for the size
  caches (`slab_init` passes flags `0`).
- **No per-slab reclaim.** `kmem_cache_free` returns slots to the free list but
  never returns a fully-empty slab to the PMM; pages are only reclaimed by
  `kmem_cache_destroy`. Long-lived caches keep their peak page footprint.

## Verification

| Suite | Marker | What it proves |
|-------|--------|----------------|
| `kernel/tests/allocator_tests.c` :: `allocator_tests_run` | `[PASS] kmalloc/kfree tests` | size-class alloc/free, **real reuse** (`b == a` after free), distinctness of 64 live allocations, `zalloc` zeroes, `realloc` grows and preserves data, 64 KiB large path, 200-round churn |
| `kernel/tests/slab_tests.c` :: `slab_tests_run` | `[PASS] slab allocator tests` | cache create, `ZERO` flag, free/reuse same slot, slab growth (`slab_count`/`grow_count >= 1`), no aliasing across 64 objects, balanced `alloc_count == free_count`, redzone object frees clean |

Both run inside `kernel_core_tests_run()` and are gated by
`make verify-kernel-core-expanded` (and thus `make verify-production-200k`).

Notable assertions worth calling out:

- `allocator_tests.c` asserts `(uintptr_t)b == (uintptr_t)a` after `kmem_free(a)`
  then `kmem_alloc(64)` — this is the regression test that distinguishes the real
  reclaiming allocator from the legacy bump heap.
- `slab_tests.c` allocates 64 widgets to force `cache_grow` and checks
  `c->slab_count >= 1 && c->grow_count >= 1`, then verifies each object's stamped
  value survived (no slot aliasing), and that `c->alloc_count == c->free_count`
  after freeing everything.

## Future expansion

- **Empty-slab reclaim.** Track `inuse` per slab (the field already exists) and
  return a slab to `pmm_free_contig` when it drops to zero, so caches shrink under
  pressure instead of holding their peak.
- **Magazine / per-CPU caches.** Add per-CPU free magazines in front of the shared
  `free_list` to remove the lock that SMP will require on `kmem_cache_alloc`.
- **Header-free slab indexing.** The 16-byte `kmem_hdr` costs space on small
  allocations; deriving the owning slab/cache from the page-aligned slab base (as
  classic SLUB does) would let small allocations drop the header.
- **Front/back redzones + quarantine.** Extend redzone mode with a leading canary
  and a free quarantine to catch underflows and use-after-free, gated by the same
  `KMEM_CACHE_REDZONE` flag.
- **Statistics export.** Surface `kmem_get_stats`/`kmem_dump_caches` through a
  `/proc`-style node so userland `meminfo`/`slabinfo` can read live cache state.
- **Promote the heap.** Replace the non-reclaiming bump heap entirely once all
  boot-time callers are migrated to `kmem_alloc`, or back `kmalloc` with the size
  classes so even early allocations reclaim.
