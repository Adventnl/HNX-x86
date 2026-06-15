# Debugging & Tracing Infrastructure

The debug subsystem (`kernel/debug/`) is the kernel's post-mortem and
observability toolkit: an in-memory log ring (dmesg), a categorized trace ring, a
symbol registry, a frame-pointer backtrace, a registrable dump framework, and the
panic/exception path that ties them together. All of it is brought up early
(right after the heap, in `kernel_core_init`) so every later subsystem can trace,
log, and register dumpers.

## Architecture

```
   kernel_log* / kdprintf  ──► COM1 + framebuffer (live sinks)
            │
            └──► klog_ring (64 KiB byte ring, "dmesg")

   KTRACE(cat, id, msg, a0, a1) ──► ktrace ring (1024 events, mask-gated)

   code address ──► ksym_resolve ──► "name+0xNN"
            ▲
   backtrace_capture(rbp) walks [rbp]->saved rbp, [rbp+8]->return addr

   debug_register_dumper("name", fn, ctx)
            │
   debug_dump_all() / debug_dump_one("slab")  ──► runs registered dumpers
            (heap, slab, objects, trace, log + subsystem-registered ones)

   CPU exception ──► exceptions.c dump ("CPU EXCEPTION" + regs [+ page-fault])
            │
            └──► kernel_panic(msg) ──► red banner + halt forever
```

The framework is designed to stay compiled in: a disabled trace category costs a
single mask test, and the dump registry is a fixed array.

## File map

| File | Responsibility |
|------|----------------|
| `kernel/debug/klog_ring.c/.h` | 64 KiB byte ring mirroring every log line (dmesg). |
| `kernel/debug/ktrace.c/.h` | category-masked event ring (1024 events). |
| `kernel/debug/ksym.c/.h` | address → nearest-symbol registry (256 entries). |
| `kernel/debug/backtrace.c/.h` | frame-pointer stack walk + symbolized print. |
| `kernel/debug/dump.c/.h` | registrable dumper framework + core dumpers + hex dump. |
| `kernel/src/panic.c`, `kernel/include/panic.h` | `kernel_panic`, `kernel_panic_hex`, `kernel_halt_forever`. |
| `kernel/arch/x86_64/exceptions.c` | CPU-exception register dump + page-fault detail, ends in panic. |
| `kernel/tests/debug_tests.c` | unit tests for log ring / trace / symbols / backtrace / objects / dump. |
| `kernel/tests/kernel_core_tests.c` | `kernel_core_init` wiring (`klog_ring_init`, `ktrace_init`, `ksym_init`, `debug_dump_init`). |

## Data structures

### Log ring (`klog_ring.h`)

`KLOG_RING_SIZE` = 64 KiB. Backed by a `struct ringbuf` over a static byte array;
oldest bytes are overwritten when full (`ringbuf_force_putc`). Tracks `g_total`
(lifetime bytes) and `g_dropped` (overwritten bytes).

### Trace ring (`ktrace.h`)

```c
struct ktrace_event { uint64_t seq; uint64_t tick; enum ktrace_cat cat;
                      uint32_t id; uint64_t arg0; uint64_t arg1; const char *msg; };
```

`KTRACE_RING_EVENTS` = 1024. Categories (`enum ktrace_cat`): `SCHED`, `MEM`,
`IRQ`, `FS`, `BLOCK`, `NET`, `USB`, `SYSCALL`, `DRIVER` (plus `_MAX`). A 32-bit
`g_mask` enables categories; `seq` is a monotonic lifetime counter, `tick` is
`kernel_ticks()`.

### Symbol registry (`ksym.h`)

```c
struct ksym { uint64_t addr; const char *name; };
```

`KSYM_MAX` = 256, kept address-sorted by insertion sort in `ksym_add` so
`ksym_resolve` is a forward scan for the nearest symbol at or below an address.

### Dump framework (`dump.h`)

```c
typedef void (*dumper_fn)(void *ctx);
struct dumper_entry { const char *name; dumper_fn fn; void *ctx; };
```

`DUMP_MAX_DUMPERS` = 32. `debug_dump_init` registers the five core dumpers:
`heap`, `slab`, `objects`, `trace`, `log`.

### Backtrace (`backtrace.h`)

`BACKTRACE_MAX_FRAMES` = 32. No struct — callers pass a `uint64_t frames[]`.

## Key APIs

### Log ring (dmesg)

- `void klog_ring_init(void)` — reset the ring.
- `void klog_ring_write(const char *)` / `klog_ring_write_n(s, n)` — append (the
  logger mirrors every line here).
- `size_t klog_ring_snapshot(out, max)` — copy up to `max` of the most recent
  bytes without consuming the ring.
- `size_t klog_ring_used(void)`, `uint64_t klog_ring_total_bytes(void)`,
  `uint64_t klog_ring_dropped(void)`.
- `void klog_ring_dump(void)` — print the ring contents bracketed by
  `---- kernel log ring (dmesg) ----` / `---- end log ring ----`.

### Tracing

- `void ktrace_init(void)`; `ktrace_enable/disable(cat)`,
  `ktrace_enable_all/disable_all()`, `int ktrace_enabled(cat)`.
- `void ktrace_emit(cat, id, msg, arg0, arg1)` — record one event.
- `KTRACE(cat, id, msg, a0, a1)` — the cheap macro: emits only if the category is
  enabled.
- `uint64_t ktrace_count(void)`, `size_t ktrace_snapshot(out, max)` (newest
  first), `const char *ktrace_cat_name(cat)`, `void ktrace_dump(max)`.

### Symbols & backtrace

- `void ksym_init(void)`, `int ksym_add(addr, name)`,
  `const char *ksym_resolve(addr, *offset_out)`, `size_t ksym_count(void)`.
- `size_t backtrace_capture(frames, max, start_rbp)` — walk the rbp chain (uses
  the current rbp if `start_rbp == 0`); returns frame count.
- `void backtrace_print(label)` / `backtrace_print_from(rbp, label)` — capture +
  symbolize, printing `#N <addr> name+0xNN` (or just `#N <addr>` when unresolved)
  under a `---- backtrace: <label> (N frames) ----` header.

### Dump framework

- `void debug_dump_init(void)` — register the core dumpers.
- `int debug_register_dumper(name, fn, ctx)` — subsystems add their own
  (process/device/irq/mount/…); returns -1 if the table is full.
- `int debug_dump_one(name)` — run one named dumper (0 if found, -1 otherwise).
- `void debug_dump_all(void)` — run all, bracketed by
  `######## DEBUG DUMP (all) ########` / `######## END DEBUG DUMP ########`.
- Helpers: `void dump_hex(data, len, base_addr)` (16-byte rows with ASCII column),
  `void dump_memory(addr, len)`; core dumpers `dump_heap/slab/objects/trace/log`.

### Panic / halt

- `void kernel_halt_forever(void)` — `cli` then `hlt` loop (noreturn).
- `void kernel_panic(const char *)` — `cli`, turn the framebuffer red, print
  `[PANIC] <message>`, halt.
- `void kernel_panic_hex(const char *, uint64_t)` — panic plus a 64-bit value.
- Legacy aliases `panic()` / `khalt()` forward to these.

## Invariants

- **Live sinks vs. ring are independent.** `klog_ring` mirrors log text but is
  decoupled from COM1 and the framebuffer, so the post-mortem history survives a
  framebuffer fault and can later back a userland `dmesg`.
- **Ring buffers never block or fault.** The log ring overwrites oldest bytes and
  counts drops; the trace ring wraps; the symbol table rejects overflow with -1.
- **Tracing is near-free when off.** `KTRACE` is a single `ktrace_enabled` mask
  test before any work; categories are toggled at runtime without recompiling.
- **Backtrace requires frame pointers.** The kernel is built at low optimization
  with the classic rbp chain (`[rbp]` = saved rbp, `[rbp+8]` = return address).
  The walker rejects implausible links: addresses below `KERNEL_PHYSICAL_BASE`,
  non-8-byte-aligned/zero rbp, and frame pointers that do not grow upward (loop
  guard).
- **Symbols resolve to the nearest preceding entry.** `ksym_resolve` returns the
  greatest `addr <= query` (and its offset), or NULL below all symbols.
- **Self-init.** `klog_ring_write` and `ktrace_emit` lazily init if called before
  `kernel_core_init`, so early logging is never lost.
- **Panic is terminal and interrupt-safe.** It disables interrupts first and never
  returns; for Prompt 2-era simplicity every unhandled CPU exception is fatal.

## Failure modes

- Log ring full → oldest bytes overwritten, `klog_ring_dropped()` increments; no
  error to the caller.
- Trace ring full → wraps; only the most recent 1024 events are retained.
- Symbol table full (>256) → `ksym_add` returns -1; unresolved addresses print as
  raw hex.
- Dumper table full (>32) → `debug_register_dumper` returns -1.
- Backtrace stops early (returns fewer frames) on a corrupt/looping chain rather
  than faulting.
- A kernel-mode CPU exception prints the register dump and calls `kernel_panic`
  ("unhandled CPU exception") → halt. A user-mode (CPL3) fault is instead routed
  to `user_fault_handle` and isolated (the process is killed, the kernel
  survives).

## Worked behavior

### Log ring vs. live output

`klog_ring_write` is fed every line the logger emits, but it is a *separate* sink
from COM1 and the framebuffer. `klog_ring_snapshot(out, max)` copies the most
recent `max` bytes without consuming the ring — if `used > max` it skips the
oldest `used - max` bytes — so a caller can dump the tail of the log after a fault
even though the live serial output already scrolled away. The ring is built on
`struct ringbuf` with `ringbuf_force_putc`, which overwrites the oldest byte when
full and signals the loss, incrementing `g_dropped`. `klog_ring_dump` prints the
whole ring between `---- kernel log ring (dmesg) ----` and `---- end log ring
----` markers.

### Trace category gating

`ktrace_emit` writes into `g_events[g_head % 1024]`, stamping `seq` (lifetime
counter) and `tick` (`kernel_ticks()`). The `KTRACE` macro guards the call with
`ktrace_enabled(cat)`, which is one `(g_mask & (1u << cat))` test, so an unused
category is essentially free. `ktrace_snapshot` returns events newest-first by
walking backward from `g_head`; `ktrace_dump(max)` formats each as
`[seq] t=<tick> <cat> id=<id> <msg> a0=.. a1=..`. The `debug_tests.c` suite emits
into an enabled `SCHED` category and a disabled `NET` category and asserts only
the enabled events were recorded.

### A dump in practice

`debug_dump_one("slab")` finds the registered `slab` dumper and runs
`dump_slab`, which fetches `kmem_stats` and prints
`slab: allocs=.. frees=.. live=.. bytes=.. large=.. caches=..` then
`kmem_dump_caches()`. `debug_dump_all()` runs every registered dumper in
registration order under the `######## DEBUG DUMP (all) ########` banner — the
single "dump everything" entry point intended for post-mortem use. Subsystems
extend the picture by calling `debug_register_dumper("process", …)` etc.; the
table holds up to `DUMP_MAX_DUMPERS` (32).

## The panic / exception path

On any CPU exception, `exceptions.c` prints (via the logger, so it also lands in
the log ring):

```
==================== CPU EXCEPTION ====================
  vector     : 0x....
  name       : #PF Page Fault        (from x86_exception_name)
  error code : 0x....
  rip / rsp / rflags / cs / ss
  mode       : kernel (CPL0) | user (CPL3)
```

For a page fault it adds `fault addr (cr2)` and `error code` detail
(`page_fault_dump`). A kernel fault then calls `kernel_panic`, which switches the
framebuffer to red, prints `[PANIC] unhandled CPU exception`, and halts. These
exact strings are what `make verify-exception` (`CPU EXCEPTION`,
`vector ... 0x06`, `#UD Invalid Opcode`) and `make verify-pagefault`
(`CPU EXCEPTION`, `#PF Page Fault`, `fault addr`, `error code`) grep for.

## Verification

The debug subsystem is exercised by `debug_tests_run` (`debug_tests.c`), part of
the Work Unit A matrix. Run it via:

```
make verify-kernel-core-expanded
```

which asserts (among the Work Unit A markers) `[PASS] debug dump tests` and
`[OK] Kernel core production foundation online`. `debug_tests.c` checks:

- log ring snapshot equals `"hello world\n"` and `klog_ring_used() == 12`;
- trace ring records only enabled categories (`ktrace_count() == 2` after one
  disabled emit), newest-first snapshot, preserved args;
- `ksym_resolve(0x1040)` → `"alpha"` offset `0x40`, `0x2500` → `"beta"`, below all
  symbols → NULL;
- `backtrace_capture` returns ≥ 1 frame;
- the object model (refcount release-on-zero, registry, handle table);
- the dump framework registers ≥ 5 core dumpers, `debug_dump_one("slab")`
  succeeds, an unknown name returns -1.

The panic/exception path is verified by the destructive `make verify-exception`
and `make verify-pagefault` targets (see `docs/testing.md`), which build with
`MYOS_TEST_INVALID_OPCODE` / `MYOS_TEST_PAGE_FAULT`, confirm the `CPU EXCEPTION`
dump markers, then rebuild the normal image.

For live debugging, `make debug` starts QEMU halted with a gdb stub on `:1234`:

```
gdb build/kernel/kernel.elf -ex 'target remote :1234'
```

## Boot wiring

`kernel_core_init` (`kernel/tests/kernel_core_tests.c`) brings the debug
framework up immediately after the heap, before any other subsystem:

```c
slab_init();
kobject_subsystem_init();
klog_ring_init();
ktrace_init();
ksym_init();
debug_dump_init();
kernel_log_ok("Kernel debug/trace framework online");
```

Because this runs so early (it only needs the PMM and heap, not the scheduler),
every later subsystem can `KTRACE`, register a dumper, and have its log mirrored
into the ring from the moment it initializes. The lazy-init guards in
`klog_ring_write` and `ktrace_emit` mean even logging that happens before this
point is not lost.

## Future expansion

- **Full symbol table from `llvm-nm`.** `ksym` currently holds a curated set of
  boot/entry symbols; a build step emitting the complete table (and raising
  `KSYM_MAX`) would make every backtrace fully symbolized.
- **Userland `dmesg` and `/proc`-style nodes.** `klog_ring_snapshot` and
  `ktrace_snapshot` are ready to back a `/dev/kmsg` or `/proc/dmesg` interface and
  a trace export.
- **Recoverable faults.** Today every kernel exception panics; demand paging,
  copy-on-write, and guard-page handling all require returning from the page-fault
  handler instead of panicking.
- **Backtrace into the panic dump.** Wire `backtrace_print_from(frame->rbp, ...)`
  and `debug_dump_all()` into the panic path for a one-shot post-mortem.
- **Per-CPU rings.** With SMP, the log and trace rings need per-CPU buffers (or
  locking) and CPU-id tagging to stay lock-free and correctly ordered.
- **Trace timestamps in real time** (TSC-based) and a binary trace format for
  off-target analysis tooling.
- **Watchpoints / assert categories** and a `BUG_ON`/`WARN_ON` family layered on
  `kernel_panic` for cheap invariant checks throughout the tree.
