# Kernel Core: data structures and object model

Work Unit A's foundation layer: the generic data structures in `kernel/lib/` and
the kernel object model in `kernel/object/`. These are the building blocks every
later subsystem (allocators, VFS, drivers, USB, networking) reuses, so they are
deliberately small, dependency-light, and individually unit-tested at boot.

## Architecture

Two layers:

1. **`kernel/lib/`** — pure, allocation-light data structures. Most are
   header-only (`list.h`, `queue.h`, `kmath.h`) or header + a small `.c`. They
   depend on `types.h` and, where they own memory, on the heap (`kmalloc`/`kfree`,
   `kcalloc`). They never touch the scheduler or hardware.
2. **`kernel/object/`** — the refcounted, typed object model layered on top of
   `kernel/sync/atomic.h` (for the refcount) and `kernel/lib/list.h` (for the
   registry). `kobject` plus a global registry plus per-context handle tables.

```
   kernel/object  (kobject, registry, handle_table)
        |  uses refcount(atomic), list, slab
   kernel/lib     (list, queue, hashtable, bitmap, ringbuf, strbuf,
                   radix, idr, fmt, kmath)
        |  uses
   types.h, heap.h (kmalloc/kfree), atomic.h
```

The structures fall into families:

- **Linkage:** `list` (intrusive doubly-linked circular), `queue` (FIFO wrapper).
- **Maps:** `hashtable` (64-bit key chained), `radix` (64-ary sparse tree),
  `idr` (small-integer allocator over a bitmap).
- **Bit/byte storage:** `bitmap`, `ringbuf`.
- **Text:** `strbuf` (bounded builder), `fmt` (`ksnprintf`/`kdprintf`).
- **Arithmetic:** `kmath` (overflow-checked add/mul, alignment, log2).
- **Objects:** `refcount`, `kobject`, `handle_table`.

## File map

| File | Purpose |
|------|---------|
| `kernel/lib/list.h` | Intrusive doubly-linked circular list (`list_node`), iteration macros, `container_of`/`list_entry` |
| `kernel/lib/list.c` | Out-of-line `list_length`, `list_contains`, `list_validate`, `list_sort` |
| `kernel/lib/queue.h` | FIFO `queue` wrapper over the list with a maintained `length` |
| `kernel/lib/hashtable.h` | Chained hash table API: 64-bit key -> `void*` |
| `kernel/lib/hashtable.c` | SplitMix64 / FNV-1a hashing, power-of-two buckets, put/get/remove/foreach |
| `kernel/lib/bitmap.h` | Fixed-storage bitmap over caller-owned 64-bit words |
| `kernel/lib/bitmap.c` | set/clear/test/range, SWAR popcount, first-zero/first-set, zero-run finder, alloc |
| `kernel/lib/ringbuf.h` | Byte ring buffer (SPSC-friendly) over caller storage |
| `kernel/lib/ringbuf.c` | putc/getc/peek, bulk write/read, overwriting `force_putc` |
| `kernel/lib/strbuf.h` | Bounded string builder with truncation tracking |
| `kernel/lib/strbuf.c` | putc/puts/putn, decimal/hex number append, `strbuf_printf` |
| `kernel/lib/radix.h` | 64-ary radix tree: 64-bit key (low 36 bits) -> `void*` |
| `kernel/lib/radix.c` | insert/lookup/remove with empty-node pruning, ascending foreach |
| `kernel/lib/idr.h` | Small-integer id allocator backed by a bitmap |
| `kernel/lib/idr.c` | round-robin alloc, reserve, free, in-use test |
| `kernel/lib/kmath.h` | Overflow-checked add/mul, alignment, pow2/log2, div-round-up, min/max/clamp |
| `kernel/lib/fmt.h` | `ksnprintf`/`kvsnprintf`/`kdprintf`, `kfmt_u64`/`kfmt_hex64` |
| `kernel/object/refcount.h` | Atomic refcount: get/put/read/get_unless_zero |
| `kernel/object/object.h` | `kobject`, `kobject_type`, registry API, `handle_table` API |
| `kernel/object/object.c` | kobject lifecycle, global registry, handle-table install/lookup/close |
| `kernel/tests/lib_tests.c` | Boot self-tests for list/queue/hashtable/bitmap/ringbuf/radix/idr/strbuf |
| `kernel/tests/debug_tests.c` | Boot self-tests for refcount/registry/handle table (alongside debug) |

## Data structures

### `struct list_node` (`list.h`)

```c
struct list_node { struct list_node *next, *prev; };
```

The classic intrusive circular list. An empty list is a sentinel head whose
`next`/`prev` point back at itself, eliminating NULL checks in insert/remove. The
owning struct embeds a `list_node`; `list_entry(ptr, type, member)` recovers the
owner via `container_of` (offset subtraction). `list_linked(n)` reports whether a
node is currently in a list (requires `list_del_init`/`list_init` discipline:
`n->next != n && n->next != NULL`).

### `struct queue` (`queue.h`)

```c
struct queue { struct list_node head; size_t length; };
```

A thin FIFO over `list_node` that additionally maintains `length` so `O(1)`
`queue_length` is available without walking. `enqueue` appends at the tail,
`dequeue` removes the head, `push_front` is the priority/urgent path.

### `struct hashtable` / `struct hnode` (`hashtable.h`)

```c
struct hnode { uint64_t key; void *value; struct hnode *next; };
struct hashtable { struct hnode **buckets; size_t nbuckets, mask, count; };
```

Chained hash table. `nbuckets` is a power of two so the bucket index is a mask
(`hash_u64(key) & mask`), not a modulo. `count` is the live entry count.

### `struct bitmap` (`bitmap.h`)

```c
struct bitmap { uint64_t *words; size_t nbits; };
```

A fixed-storage bitmap over a *caller-provided* array of 64-bit words (attached via
`bitmap_attach`). Indices are bit positions; helpers convert to word/offset. Used
by the PMM-style allocators, slab free maps, and the `idr`.

### `struct ringbuf` (`ringbuf.h`)

```c
struct ringbuf { uint8_t *data; size_t capacity, head, tail, count; };
```

Byte ring with caller-owned storage. Keeping an explicit `count` (rather than
sacrificing a slot) lets a full buffer (`count == capacity`) be distinguished from
empty (`count == 0`) at full capacity. `head` is the next write index, `tail` the
next read index.

### `struct strbuf` (`strbuf.h`)

```c
struct strbuf { char *buf; size_t cap; size_t len; int truncated; };
```

Bounded string builder over a caller buffer. `cap` includes the NUL terminator;
`len` excludes it. `truncated` latches once any append did not fully fit, so the
caller can detect lossy formatting without checking every call.

### `struct radix_tree` / `struct radix_node` (`radix.h`, `radix.c`)

```c
struct radix_tree { struct radix_node *root; size_t count; };
struct radix_node { void *slots[RADIX_FANOUT]; size_t used; };  /* RADIX_FANOUT == 64 */
```

A 6-bit-stride (64-ary) sparse map. `RADIX_LEVELS == 6` covers 36-bit keys
(`RADIX_MAX_KEY == 2^36 - 1`); higher bits are validated and rejected. Internal
nodes hold child pointers; the leaf level holds values. `used` counts non-NULL
slots so empty nodes can be pruned on removal.

### `struct idr` (`idr.h`)

```c
struct idr {
    struct bitmap map;
    uint64_t     *storage;    /* kcalloc'd bitmap words */
    uint32_t      base, capacity, next_hint;
};
```

Small-integer handle allocator. Hands out the lowest free id in
`[base, base+capacity)`. `next_hint` is a round-robin cursor that reduces id reuse
churn (so a freed id is not immediately handed back out).

### `struct refcount` (`refcount.h`)

```c
struct refcount { atomic_t count; };
```

Atomic reference count. Convention: created at 1 (the creator's reference);
`get()` only valid while holding a reference; `put()` returns 1 exactly when the
count reaches 0 so the owner releases once. `get_unless_zero` implements a
weak-to-strong upgrade via CAS loop.

### `struct kobject` (`object.h`)

```c
struct kobject {
    struct refcount    ref;
    enum kobject_type  type;       /* KOBJ_DEVICE/FILE/PROCESS/THREAD/... */
    uint64_t           id;         /* assigned on register */
    const char        *name;
    void             (*release)(struct kobject *);
    struct list_node   registry_link;
    void              *priv;       /* owner payload */
};
```

The base for every refcounted, typed kernel object. `release` is the destructor
invoked when the count hits zero. `registry_link` threads the object onto the
global registry. `priv` lets a subsystem hang its own struct off the kobject
(usually via `container_of`).

### `struct handle_table` / `struct handle_slot` (`object.h`)

```c
struct handle_slot  { struct kobject *obj; uint32_t rights; int used; };
struct handle_table { struct handle_slot *slots; uint32_t capacity, count, next_hint; };
```

A per-context (process, driver) integer-handle namespace over kobjects. Installing
takes a reference; closing drops it. `rights` carries opaque permission bits.
`next_hint` round-robins slot allocation.

## Key APIs

### List

```c
void list_add(node, head);        /* insert after head (LIFO push / front)   */
void list_add_tail(node, head);   /* insert before head (FIFO append)        */
void list_del(node);              /* unlink; NULLs the node's pointers       */
void list_del_init(node);         /* unlink and re-init (re-addable/testable)*/
void list_move(node, head);       /* relocate to another list, front         */
void list_splice_init(src, dst);  /* move all of src after dst's head        */
size_t list_length(head);         /* O(n) walk                               */
int  list_validate(head);         /* O(n): every next->prev consistent       */
void list_sort(head, cmp);        /* stable insertion sort                   */
```

`list_validate` walks forward verifying `p->next->prev == p` and
`p->prev->next == p` for every node, bounding the walk at `0x1000000` iterations so
a corrupt cyclic list returns 0 instead of looping forever. `list_sort` is a stable
insertion sort: it empties the head, then re-inserts each node before the first
sorted node with a strictly greater key (`cmp(pos, node) <= 0` continues), so equal
keys keep their input order. Complexity `O(n^2)` worst case, fine for the small
lists the kernel sorts.

### Hash table

```c
int  hashtable_init(ht, hint);                   /* round hint up to pow2, min 8 */
int  hashtable_put(ht, key, value);              /* insert or overwrite          */
void *hashtable_get(ht, key, &found);            /* found disambiguates NULL val */
void *hashtable_remove(ht, key);                 /* returns removed value        */
void hashtable_foreach(ht, fn, ctx);
uint64_t hash_u64(uint64_t);                     /* SplitMix64 finalizer         */
uint64_t hash_bytes(const void*, size_t);        /* FNV-1a                       */
```

`hash_u64` is the SplitMix64 finalizer (good avalanche, branch-free). Buckets are
sized with `round_pow2` (start at 8, double until `>= hint`). Insert/lookup/remove
are `O(1)` expected, `O(chain)` worst case; there is **no automatic resize**, so
chains grow if load factor climbs (see Failure modes). `put` of an existing key
overwrites in place and leaves `count` unchanged.

### Bitmap

```c
void   bitmap_zero/fill(b);
void   bitmap_set/clear(b, bit);  int bitmap_test(b, bit);
void   bitmap_set_range/clear_range(b, start, count);
size_t bitmap_weight(b);                  /* SWAR popcount over all words      */
size_t bitmap_find_first_zero/first_set(b);   /* ctz scan; BITMAP_NONE if none */
size_t bitmap_find_zero_run(b, count);    /* first run of count zero bits      */
size_t bitmap_alloc(b);                   /* find first zero, set it, return   */
```

`bitmap_fill` masks off padding bits in the final word so `bitmap_weight` stays
exact. `weight` is `O(words)` via the SWAR `popcount64`. `find_first_zero/set` scan
word-at-a-time and use a count-trailing-zeros (`ctz64`) within the first non-trivial
word — `O(words)`. `find_zero_run` is a linear `O(nbits)` bit scan. Out-of-range bit
indices are silently ignored by set/clear and read as 0 by test.

### Ring buffer

```c
int    ringbuf_putc(r, c);          /* 0 if full                              */
int    ringbuf_getc(r, &out);       /* 0 if empty                             */
int    ringbuf_peek(r, &out);
size_t ringbuf_write/read(r, buf, n);  /* bulk; returns count transferred     */
int    ringbuf_force_putc(r, c);    /* overwrite oldest when full; returns 1 if dropped */
```

All operations are `O(1)` (bulk is `O(n)`). Indices advance with `% capacity`.
`force_putc` is the lossy-log path: when full it advances `tail` (drops the oldest
byte) before writing.

### String builder + formatting

```c
void strbuf_putc/puts/putn(sb, ...);
void strbuf_put_u64/i64/hex(sb, v);
void strbuf_printf(sb, fmt, ...);     /* via a 256-byte temp + kvsnprintf      */
int  ksnprintf(buf, size, fmt, ...);  /* returns chars that *would* be written */
int  kdprintf(fmt, ...);              /* format then kernel_log()              */
```

`ksnprintf`/`kvsnprintf` support the practical printf subset
`%d %i %u %x %X %p %s %c %%` with width, `0`/`-`/`+`/` ` flags and `l`/`ll`/`z`
length modifiers. No floating point (the kernel is `-msoft-float`). The return value
is the would-be length, so `>= size` signals truncation.

### Radix tree

```c
int   radix_insert(t, key, value);   /* -1 on key>MAX or value==NULL or OOM    */
void *radix_lookup(t, key);          /* NULL on miss                           */
void *radix_remove(t, key);          /* prunes now-empty internal nodes        */
void  radix_foreach(t, fn, ctx);     /* ascending key order                    */
```

Insert/lookup/remove are `O(RADIX_LEVELS)` = `O(6)` = effectively `O(1)`, allocating
internal nodes lazily on insert (`kcalloc`) and freeing them bottom-up on remove
when `used` reaches 0 (the root is freed only when it empties). `value == NULL` is
rejected on insert because NULL is the "absent" sentinel returned by lookup.

### IDR

```c
int  idr_init(idr, base, capacity);  /* kcalloc bitmap; -1 on OOM              */
int  idr_alloc(idr);                 /* lowest free id from next_hint; -1 full */
int  idr_reserve(idr, id);           /* claim a specific id; -1 if taken/OOR   */
void idr_free(idr, id);
int  idr_in_use(idr, id);
uint32_t idr_used(idr);              /* bitmap_weight                          */
```

`idr_alloc` scans from `next_hint` to the end, then wraps to the start (a two-pass
round-robin), so `O(capacity)` worst case but typically near `O(1)`.

### Object model

```c
void kobject_init(o, type, name, release);   /* ref=1, id=0, unlinked          */
void kobject_get(o);                          /* refcount_get                   */
void kobject_put(o);                          /* on last ref: unregister+release*/
void kobject_register(o);                     /* assign id (g_next_id++), link  */
struct kobject *kobject_lookup(id);           /* O(n) registry walk             */
size_t kobject_count(void);
size_t kobject_count_by_type(type);

int  handle_table_init(t, capacity);          /* kmem_zalloc slots; -1 on OOM   */
int  handle_install(t, obj, rights);          /* takes a ref; returns handle/-1 */
struct kobject *handle_lookup(t, handle, &rights);
int  handle_close(t, handle);                 /* drops the ref; 0/-1            */
void handle_table_destroy(t);                 /* drops every installed ref      */
```

`kobject_put` is the single release point: on the last `put` it auto-unregisters
(if still linked) before calling `release`. The registry assigns monotonically
increasing ids starting at 1 (0 means "unregistered"). `handle_install` round-robins
over two passes from `next_hint`, takes a reference on success, and returns the slot
index as the handle. `handle_close` clears the slot and drops the reference.
`handle_table_destroy` drops every still-installed reference, so leaking handles do
not leak the underlying object beyond table teardown.

## Invariants

- **List:** for every node `n` in a healthy list, `n->next->prev == n` and
  `n->prev->next == n` (checked by `list_validate`). An empty list head satisfies
  `head->next == head->prev == head`.
- **Queue:** `queue.length` equals the number of linked nodes; maintained by
  enqueue/dequeue/remove (which decrement only when `length != 0`).
- **Hashtable:** `nbuckets` is a power of two and `mask == nbuckets - 1`; `count`
  equals the number of live `hnode`s; a key appears in at most one bucket chain.
- **Bitmap:** padding bits beyond `nbits` in the last word are 0 after `fill`, so
  `weight` never over-counts.
- **Ringbuf:** `0 <= count <= capacity`; `head` and `tail` are always
  `< capacity`; `free + used == capacity`.
- **Strbuf:** `buf[len] == '\0'` whenever `cap > 0`; `len < cap`.
- **Radix:** every key satisfies `key <= RADIX_MAX_KEY`; `count` equals the number
  of leaf values; an internal node with `used == 0` is never left attached except
  the root.
- **IDR:** an id is "in use" iff its bit is set; `idr_used == bitmap_weight`.
- **Refcount:** while any reference is held, `count > 0`; `put` returning 1 happens
  exactly once per object.
- **Kobject:** a registered object has `id != 0` and is on `g_registry`; the last
  `put` both unregisters and releases.
- **Handle table:** a `used` slot holds a non-NULL `obj` on which the table owns one
  reference; `count` equals the number of used slots.

## Failure modes

- **Hashtable has no resize.** Under a high load factor chains lengthen and
  get/put degrade toward `O(n)`. Allocation failure in `hashtable_init`/`_put`
  returns -1 (caller must check). A stored NULL value is ambiguous with a miss
  unless the `found` out-param is used.
- **Radix rejects high keys.** `key > RADIX_MAX_KEY` (≥ 2^36) returns -1 on insert
  and NULL on lookup/remove; callers with larger key spaces must use the hashtable.
  `radix_insert(value=NULL)` also fails by design.
- **Bitmap/ringbuf/strbuf are bounds-quiet.** Out-of-range bitmap ops are ignored;
  a full ringbuf rejects `putc` (returns 0) — callers must check the return; strbuf
  silently sets `truncated` rather than overflowing.
- **List misuse.** `list_linked` only works after `list_del_init`/`list_init` (not
  plain `list_del`, which NULLs the pointers). Double-deleting a node that was
  `list_del`'d corrupts neighbors.
- **Refcount underflow.** Calling `put` more times than `get` drives the count
  negative and triggers a premature `release`; the get/put discipline is the
  caller's responsibility.
- **Handle exhaustion.** `handle_install` returns -1 when every slot is used; the
  caller must handle the failure (no auto-grow).
- **Registry lookup is linear.** `kobject_lookup` walks `g_registry`; it is a
  diagnostic/debug path, not a hot lookup. Use a handle table or hashtable for
  performance-sensitive id->object maps.

## Verification

All of these are exercised at boot by Work Unit A and surfaced as `[PASS]` markers
grepped by `make verify-kernel-core-expanded`:

| Suite (file) | Marker(s) |
|--------------|-----------|
| `kernel/tests/lib_tests.c` :: `test_list` | `[PASS] lib list tests` |
| `kernel/tests/lib_tests.c` :: `test_hashtable` | `[PASS] hash table tests` |
| `kernel/tests/lib_tests.c` :: `test_bitmap` | `[PASS] bitmap tests` |
| `kernel/tests/lib_tests.c` :: `test_ringbuf` | `[PASS] ring buffer tests` |
| `kernel/tests/lib_tests.c` :: `test_radix_and_idr` | `[PASS] lib radix/idr/strbuf tests` |
| `kernel/tests/debug_tests.c` (object portion) | `[PASS] debug dump tests` |

What the tests actually assert (representative):

- **list:** length after adds, `list_validate` after delete, first/last entries,
  stable ascending sort, and the queue wrapper's FIFO order.
- **hashtable:** 100 puts -> count 100, get value, overwrite (count unchanged),
  miss, remove (count 99), and `foreach` visits all 99.
- **bitmap:** weight, first-zero/first-set, range set (`set_range(10,20)` -> weight
  20, exclusive bounds), `find_zero_run(8)` at 100 but no 9-bit run, and
  `bitmap_alloc` returning 0 then 1.
- **ringbuf:** fill to capacity, reject when full, FIFO drain, bulk roundtrip via
  `memcmp`, and `force_putc` dropping the oldest bytes.
- **radix/idr/strbuf:** 500 sparse inserts -> count 500, lookup/miss/remove,
  `idr_alloc` returns base then base+1, double-reserve fails, and a `strbuf` that
  builds `"x=42 h=0xbeef"`.
- **object:** refcount get/put with release-on-zero, registry unique ids and
  lookup/count-by-type, and a handle table whose install takes a reference and
  whose close drops it (`kobject_refs` 1 -> 2 -> 1).

The whole matrix runs from `kernel_core_tests_run()` (after `kernel_core_init()`
brings up the slab allocator and object subsystem) in
`kernel/tests/kernel_core_tests.c`.

## Future expansion

- **Resizable hashtable.** Add a load-factor trigger and rehash so large caches
  (inode, dentry) stay `O(1)` without a hand-tuned initial `hint`.
- **Wider radix keys.** Add a level (or a configurable height) to cover full
  64-bit keys, or a tagged-pointer variant for inline small values.
- **RCU-style list traversal.** The `list_for_each_*_safe` macros already allow
  delete-during-iteration; a future SMP phase would add proper RCU read-side
  protection rather than relying on single-CPU semantics.
- **Per-CPU idr/bitmap caches.** When SMP arrives, the round-robin `next_hint`
  cursor in `idr`/`handle_table` becomes contended; per-CPU free stacks would
  remove the global scan.
- **Typed handle rights.** `handle_slot.rights` is opaque today; a capability model
  (read/write/exec/transfer bits with checks in `handle_lookup`) is the natural
  evolution for the syscall surface.
