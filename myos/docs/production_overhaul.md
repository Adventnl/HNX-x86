# Production Overhaul Phase 1

This document describes the *Production Overhaul Phase 1* effort: the goals, the
work-unit decomposition, the lines-of-code accounting that gates the phase, and
the marker/verification conventions that every work unit must satisfy. It is the
top-level index for the per-subsystem documents
(`kernel_core.md`, `memory_allocators.md`, `vm.md`, `synchronization.md`).

The phase takes a kernel that already boots through Prompts 2–6 (UEFI boot, GDT/
IDT/exceptions, PMM/VMM, interrupts, scheduler, user mode, VFS, storage, USB) and
hardens it into a *production-shaped* kernel: real reclaiming allocators, a kernel
object model, synchronization primitives, deferred work, timers, and a debug/trace
framework, each backed by boot-time self-tests whose pass markers are grepped out
of the QEMU serial log.

## Architecture

The overhaul is organized as a set of independent *work units*, lettered A–H.
Each work unit owns a slice of the kernel/userland, lands its own source and tests,
and appends one or more `verify-*` targets to `Makefile.production`. The aggregate
gate `verify-production-200k` (in the root `Makefile`) runs every work unit's
verification plus the legacy `verify-prompt6` chain and the LOC gate.

```
                       +-------------------------------+
                       |     verify-production-200k    |   (root Makefile)
                       +---------------+---------------+
                                       |
        +------------------------------+------------------------------+
        |                |             |              |               |
   verify-prompt6   verify-kernel-  verify-process- ... (per unit)  production-
   (legacy chain)   core-expanded   expanded                        loc-check
                         |
                  Work Unit A boots the real image, runs the test matrix,
                  emits [PASS]/[OK] markers, verify_qemu.py greps them.
```

The defining property of the architecture: **nothing is asserted by the build
system in isolation**. A `verify-*` target boots the actual kernel image under
QEMU and searches the serial log for markers that only appear if the corresponding
code ran and passed. There are no hardcoded pass strings; a regression makes a
marker disappear and the grep fails honestly.

Work Unit A (kernel core) is the foundation every other unit builds on, and is the
subject of the four sibling documents. Its code lives under `kernel/lib/`,
`kernel/sync/`, `kernel/memory/` (slab, vmregion), `kernel/object/`, `kernel/work/`,
`kernel/time/`, and `kernel/debug/`, with the test matrix under `kernel/tests/`.

### The work units

| Unit | Scope | Representative subsystems |
|------|-------|---------------------------|
| A | kernel core | lib data structures, slab/kmem allocators, VM region tracker, sync primitives, workqueue, timer callouts, object model, debug/trace |
| B | process / syscall | process model, syscall surface expansion, fd tables |
| C | vfs / fs | VFS layer, HNXFS, page cache, filesystem stress |
| D | storage / driver | PCI, AHCI, NVMe, block layer, driver lifecycle |
| E | usb / input | xHCI, USB core, HID, unified input stack |
| F | networking | ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, sockets |
| G | userland | expanded libc, shell, coreutils, services |
| H | test infra | shared `ktest` harness, stress, randomized testing |

The unit boundaries are visible in `Makefile.production`, whose `.PHONY` block
enumerates the per-unit targets, and in the body of `verify-production-200k` in the
root `Makefile`, which invokes them in dependency order (Unit A first, networking
and userland late, the LOC gate last).

### Mapping work units to `verify-*` targets

Each unit contributes one or more targets to `verify-production-200k`. The mapping
below is read directly off the body of that target in the root `Makefile` and the
`.PHONY` declarations in `Makefile.production`:

| Unit | `verify-*` targets in `verify-production-200k` |
|------|-----------------------------------------------|
| A | `verify-kernel-core-expanded`, `verify-test-infra` |
| B | `verify-process-expanded`, `verify-syscall-expanded` |
| C | `verify-vfs-expanded`, `verify-hnxfs-expanded`, `verify-page-cache`, `verify-fs-stress` |
| D | `verify-driver-expanded`, `verify-pci-expanded`, `verify-ahci-expanded`, `verify-nvme`, `verify-block-expanded` |
| E | `verify-xhci-expanded`, `verify-usb-expanded`, `verify-hid-expanded`, `verify-input-expanded` |
| F | `verify-network` (and the per-protocol `verify-ethernet`/`-arp`/`-ipv4`/`-icmp`/`-udp`/`-dhcp`/`-dns`/`-sockets` declared in `Makefile.production`) |
| G | `verify-libc-expanded`, `verify-shell-expanded`, `verify-coreutils-expanded`, `verify-services` |
| H | `verify-test-infra`, `verify-stress`, `verify-randomized` |

Two cross-cutting targets bracket the unit-specific ones: `verify-prompt6` (the
entire legacy regression chain) runs first so a foundational break fails fast, and
`verify-qemu-matrix` (the five-memory-size boot matrix) plus `production-loc-check`
run last so the phase is only green when both the new functionality *and* the LOC
floor hold across every memory configuration.

### Where Phase 1 wires into boot

Work Unit A is brought up in `kernel/src/kernel.c` immediately after the heap, so
every later subsystem can rely on `kmem_alloc`, the object registry, and the
trace/dump infrastructure:

```
heap_init();                 /* legacy bump heap */
kernel_log_ok("Kernel heap online");
...
kernel_core_init();          /* slab_init + kobject + klog_ring + ktrace + ksym + dump */
run_destructive_tests();     /* test-build-only fault injection */
early_tests_run();           /* Prompt 2.5 baseline */
kernel_core_tests_run();     /* Work Unit A test matrix -> [PASS] markers */
```

`kernel_core_init()` and `kernel_core_tests_run()` are declared in
`kernel/tests/kernel_core_tests.h` and implemented in
`kernel/tests/kernel_core_tests.c`.

## File map

| File | Purpose |
|------|---------|
| `Makefile` | Root build + run + all `verify-*` targets; defines `production-loc-check` and `verify-production-200k`; `-include`s `Makefile.production` |
| `Makefile.production` | Per-work-unit `verify-*` targets appended as each unit lands; Work Unit A's `verify-kernel-core-expanded` lives here |
| `kernel/src/kernel.c` | Boot entry; calls `kernel_core_init()` then `kernel_core_tests_run()` after the heap is online |
| `kernel/tests/kernel_core_tests.h` | Declares the Work Unit A init + per-suite test entry points |
| `kernel/tests/kernel_core_tests.c` | `kernel_core_init()` (subsystem bring-up) and `kernel_core_tests_run()` (the test matrix aggregator) |
| `kernel/tests/ktest.h` | The shared in-kernel test harness (`KT_BEGIN`/`KT_CHECK`/`KT_RESULT`) used by every suite |
| `tools/verify_qemu.py` | Boots the image under QEMU and asserts each `--expect` marker appears in the serial log |
| `docs/kernel_core.md` | Work Unit A: lib data structures + object model |
| `docs/memory_allocators.md` | Work Unit A: bump heap, slab + kmem_cache, size-class allocator |
| `docs/vm.md` | Work Unit A: VM region tracker |
| `docs/synchronization.md` | Work Unit A: atomics, locks, waitqueues, blocking primitives |

## Data structures

Phase 1 itself has no runtime data structures of its own; it is a build/verify
methodology layered over the per-unit subsystems. The structures that matter here
are the *conventions* encoded as make variables and shell logic.

### The LOC gate (`production-loc-check`)

```make
PROD_LOC_MIN := 200000
PROD_LOC_FIND = find bootloader kernel user shared tools docs -type f \
    \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \
       -o -name '*.py' -o -name '*.md' -o -name '*.spec' \)
```

Field meanings:

- `PROD_LOC_MIN` — the floor the project must stay above (200,000 useful LOC).
- `PROD_LOC_FIND` — the file set that counts as "useful": C/headers, assembly,
  linker scripts, Python tooling, Markdown docs, and `.spec` files, scanned only
  under the six source trees `bootloader kernel user shared tools docs`.

### The marker grammar

Every self-test emits one of a tiny fixed vocabulary of line prefixes. These are
defined by `ktest.h` (for test results) and by `kernel_log_ok` (for subsystem
bring-up):

| Marker | Meaning | Emitted by |
|--------|---------|-----------|
| `[OK] <text>` | a subsystem came online during boot | `kernel_log_ok()` |
| `[PASS] <marker>` | a named self-test passed (all `KT_CHECK`s succeeded) | `KT_RESULT()` |
| `[FAIL] <marker>` | a named self-test failed (at least one check failed) | `KT_RESULT()` |
| `[CHECK FAILED] ...` | the specific failing condition, file and line | `KT_CHECK()` |
| `[PANIC] ...` | a fatal assertion aborted the boot | panic path |

The `Makefile.production` header states this convention verbatim so every future
work unit follows it:

```
#   [OK]   subsystem came online during boot
#   [PASS] a named self-test passed
#   [FAIL] / [PANIC] a self-test failed (boot aborts)
```

The crucial discipline is that the verifier (`tools/verify_qemu.py --expect`) only
ever greps for `[OK]`/`[PASS]` strings. It can therefore never be satisfied by a
string the build system itself prints — the string must be emitted by kernel code
that ran far enough to print it. A regression makes the marker vanish; it cannot be
faked.

### Why marker-by-absence is the design

A conventional test runner reports `PASS`/`FAIL` from the runner's own logic, which
can drift from what the code does. Here the *kernel* decides, via `KT_RESULT`,
whether to print `[PASS]` or `[FAIL]`, and the build merely greps for the success
string. Three consequences:

- A test that never runs (e.g. boot panicked earlier) produces no marker, so the
  grep fails — silent skips are impossible.
- A test that runs and fails prints `[FAIL] <marker>` plus `[CHECK FAILED] ...`,
  and the `--expect "[PASS] <marker>"` grep still fails — the failure is visible
  twice.
- There is exactly one place (`KT_RESULT`) that can emit a success marker, so the
  vocabulary cannot leak into ordinary log lines.

## Key APIs

The "APIs" of Phase 1 are make targets and the in-kernel test harness.

### `make production-loc-check`

Counts useful LOC and compares against `PROD_LOC_MIN`:

```make
production-loc-check:
	@total=$$($(PROD_LOC_FIND) -exec cat {} + | wc -l | tr -d ' '); \
	  printf 'production useful LOC : %s\n' "$$total"; \
	  printf 'production target     : %s\n' "$(PROD_LOC_MIN)"; \
	  if [ "$$total" -lt $(PROD_LOC_MIN) ]; then \
	    need=$$(( $(PROD_LOC_MIN) - total )); \
	    echo "[FAIL] production LOC below target"; \
	    ... exit 1; \
	  fi; \
	  printf '[PASS] production LOC >= %s (have %s)\n' "$(PROD_LOC_MIN)" "$$total"
```

Behavior:

- Concatenates every matching file (`cat {} +`) and counts lines with `wc -l`.
- On shortfall it prints `[FAIL] production LOC below target`, the current count,
  and the remaining LOC needed, then `exit 1` (fails the build).
- On success it prints `[PASS] production LOC >= 200000 (have N)`.

The counting methodology is deliberately conservative-by-construction: build
artifacts under `build/`, object files, ELF/EFI binaries, disk images, and
firmware all live outside the six scanned trees or carry non-source extensions,
so they cannot inflate the count. Generated code is not counted because it is not
checked into the scanned trees.

### `make verify-production-200k`

The aggregate phase gate (root `Makefile`). It runs, in order:

1. `verify-prompt6` — the entire legacy chain (boot, exceptions, page fault,
   interrupts, timer, scheduler, preemption, user mode, syscalls, VFS, process,
   shell, PCI, block, storage, HNXFS, keyboard, TTY, MSI, driver lifecycle, xHCI,
   USB, HID, unified input, hardware userland, and the QEMU memory matrix).
2. Every Phase 1 per-unit target: `verify-kernel-core-expanded`,
   `verify-process-expanded`, `verify-syscall-expanded`, `verify-vfs-expanded`,
   `verify-hnxfs-expanded`, `verify-page-cache`, `verify-fs-stress`,
   `verify-driver-expanded`, `verify-pci-expanded`, `verify-ahci-expanded`,
   `verify-nvme`, `verify-block-expanded`, `verify-xhci-expanded`,
   `verify-usb-expanded`, `verify-hid-expanded`, `verify-input-expanded`,
   `verify-network`, `verify-libc-expanded`, `verify-shell-expanded`,
   `verify-coreutils-expanded`, `verify-services`, `verify-test-infra`,
   `verify-stress`, `verify-randomized`, `verify-qemu-matrix`.
3. `production-loc-check` — the LOC gate, last.

On full success it prints `[OK] verify-production-200k passed`. Because every step
is a sub-`$(MAKE)`, the first failing sub-target aborts the whole gate.

### `make verify-kernel-core-expanded`

Work Unit A's verification (in `Makefile.production`). It builds the image and
greps the boot serial log for all thirteen `[PASS]` markers plus the unit's
`[OK]` line:

```make
verify-kernel-core-expanded: image
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name kernel-core --timeout 90 \
	  --log $(B_IMG)/kernel-core.log \
	  --expect "[PASS] lib list tests" \
	  --expect "[PASS] hash table tests" \
	  --expect "[PASS] bitmap tests" \
	  --expect "[PASS] ring buffer tests" \
	  --expect "[PASS] kmalloc/kfree tests" \
	  --expect "[PASS] slab allocator tests" \
	  --expect "[PASS] VM region tests" \
	  --expect "[PASS] wait queue tests" \
	  --expect "[PASS] mutex tests" \
	  --expect "[PASS] rwlock tests" \
	  --expect "[PASS] workqueue tests" \
	  --expect "[PASS] timer callout tests" \
	  --expect "[PASS] debug dump tests" \
	  --expect "[OK] Kernel core production foundation online"
```

### The `ktest` harness

The test harness is the contract between code and the verifier. From
`kernel/tests/ktest.h`:

```c
#define KT_BEGIN()          int __kt_fail = 0
#define KT_CHECK(cond, msg) /* logs [CHECK FAILED] msg (file:line) and sets __kt_fail */
#define KT_CHECK_EQ(a,b,m)  /* logs got/want on mismatch and sets __kt_fail */
#define KT_RESULT(marker)   /* prints "[PASS] marker" or "[FAIL] marker" */
```

A suite begins with `KT_BEGIN()`, runs assertions, and ends with
`KT_RESULT("its marker")`. On any failed check the marker flips to `[FAIL]`, the
`--expect "[PASS] ..."` grep fails, and the build is red — the failure surfaces by
the *absence* of the success marker, never by a forged one.

## Invariants

- Every Phase 1 work unit must add at least one `verify-*` target to
  `Makefile.production` and have it invoked by `verify-production-200k`.
- A `verify-*` target may only `--expect` markers that are emitted by code that
  actually executes during boot; no `--expect` string may be a literal echoed by
  the Makefile.
- `[OK]` is reserved for "a subsystem came online"; `[PASS]`/`[FAIL]` are reserved
  for named self-tests. The two vocabularies never overlap.
- The useful-LOC count must never drop below `PROD_LOC_MIN` (200,000). The gate
  runs last in `verify-production-200k` so a passing functional suite with a LOC
  regression still fails the phase.
- The LOC scan set is exactly `bootloader kernel user shared tools docs`; no other
  directory contributes, so build output cannot inflate the metric.
- Work Unit A (`kernel_core_init`) is brought up after the heap and before the
  scheduler, so it may use `kmem_alloc`/`kmalloc` but must not depend on threads.

## Failure modes

- **A self-test regresses.** One `KT_CHECK` fails, `KT_RESULT` prints `[FAIL]
  <marker>` instead of `[PASS]`, the `--expect "[PASS] <marker>"` grep in
  `verify_qemu.py` finds nothing, and the target exits non-zero. The
  `[CHECK FAILED] <msg> (file:line)` line above it pinpoints the assertion.
- **LOC shortfall.** `production-loc-check` prints `[FAIL] production LOC below
  target`, the current count, the remaining LOC needed, and `exit 1`. This fails
  `verify-production-200k` even when every functional test passed.
- **Boot hangs / panics before a marker prints.** The expected marker never
  appears; `verify_qemu.py` times out (e.g. `--timeout 90`) and the target fails.
  A `[PANIC]` or `CPU EXCEPTION` line in the log indicates a fatal fault.
- **A late work unit breaks a foundational one.** Because `verify-production-200k`
  runs Unit A first, a foundational regression fails fast before the dependent
  units even boot.
- **Marker collision.** Two suites sharing the same `KT_RESULT` marker string make
  the grep ambiguous; markers are kept unique per suite (e.g. `lib list tests`
  vs. `lib radix/idr/strbuf tests`).

## Verification

- `make production-loc-check` — prints the useful LOC and `[PASS]`/`[FAIL]`.
- `make verify-kernel-core-expanded` — Work Unit A's thirteen `[PASS]` markers plus
  `[OK] Kernel core production foundation online`.
- `make verify-production-200k` — the full phase gate: legacy chain + every Phase 1
  unit + the LOC gate, ending in `[OK] verify-production-200k passed`.
- `make loc` — informational per-tree LOC breakdown (bootloader/kernel/user/
  shared/tools/docs/total); not a gate, but useful when diagnosing a shortfall.

The boot-time emission of Work Unit A's markers is wired through
`kernel_core_tests_run()` in `kernel/tests/kernel_core_tests.c`, which calls each
suite (`lib_tests_run`, `allocator_tests_run`, `slab_tests_run`, `vm_tests_run`,
`sync_tests_run`, `workqueue_tests_run`, `timer_tests_run`, `debug_tests_run`) and
finishes with `kernel_log_ok("Kernel core production foundation online")`.

## Future expansion

- **Generated symbol table.** `ksym` currently registers a curated symbol set; a
  build step emitting a full table from `llvm-nm` would make panic backtraces fully
  symbolic and add genuine LOC under `tools/`.
- **Per-unit LOC sub-gates.** `production-loc-check` is a single global floor; a
  future refinement could enforce minimums per work unit so no single area starves.
- **Coverage of blocking paths.** Several sync suites only exercise uncontended
  paths in the single-threaded boot context; a later phase can add real-thread
  integration tests once the scheduler-driven harness exists (Work Unit H).
- **CI matrix.** `verify-qemu-matrix` already runs five memory sizes; additional
  axes (CPU count once SMP lands, firmware variants) would slot into the same
  marker-grep methodology.
- **`.spec` artifacts.** The LOC scan already counts `.spec` files; a formal
  specification format for each work unit could be introduced without changing the
  gate.
