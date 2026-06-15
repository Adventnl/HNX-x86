# Testing & Verification

MyOS has no userland test runner driving the kernel from outside. Instead the
kernel runs its own test matrix during boot, prints `[PASS]`/`[FAIL]` markers to
COM1, and a host-side Python tool (`tools/verify_qemu.py`) boots the image
headless in QEMU and greps the serial log for the expected markers. A marker is
only present if the code that prints it actually ran and its assertions held, so
there are no hardcoded pass strings — a regression makes a marker *disappear* and
the verification fails honestly.

## Architecture

```
   make verify-<name>
        |
        +-- make image            (bootloader + kernel + initramfs + disks)
        |
        +-- tools/verify_qemu.py --image ... --expect "MARKER" [--expect ...]
                 |
                 |  qemu-system-x86_64 -machine q35 -m <mem> + OVMF pflash
                 |  + storage.img (AHCI) + nvme.img + qemu-xhci + usb-kbd/mouse
                 |  -serial stdio -display none
                 v
            kernel boots, runs the in-kernel test matrix, prints to COM1
                 |
            reader thread captures stdout; main loop checks every --expect
            substring appears within --timeout seconds
                 |
            all found -> [PASS]   any missing -> [FAIL] + list + serial log path
```

Two layers cooperate:

1. **In-kernel harness** (`kernel/tests/ktest.h`) — a tiny `KT_CHECK` /
   `KT_RESULT` assertion macro set used by every suite.
2. **Host verifier** (`tools/verify_qemu.py`) — boots the image and matches
   serial markers, wrapped by `make verify-*` targets.

## File map

| File | Responsibility |
|------|----------------|
| `kernel/tests/ktest.h` | `KT_BEGIN`/`KT_CHECK`/`KT_CHECK_EQ`/`KT_RESULT`/`KT_FAILED` macros. |
| `kernel/tests/early_tests.c` | Earliest boot self-tests (boot_info, mem helpers, GDT/IDT, PMM, VMM, heap). Halts on failure. |
| `kernel/tests/kernel_core_tests.c` | Work Unit A: `kernel_core_init` + the core test matrix aggregator. |
| `kernel/tests/lib_tests.c` | list / hashtable / bitmap / ringbuf / radix-idr-strbuf. |
| `kernel/tests/allocator_tests.c`, `slab_tests.c` | kmalloc/kfree, slab caches. |
| `kernel/tests/vm_tests.c` | VM region allocator. |
| `kernel/tests/sync_tests.c` | spinlock / wait queue / mutex / rwlock. |
| `kernel/tests/workqueue_tests.c`, `timer_tests.c` | deferred work, timer callouts. |
| `kernel/tests/debug_tests.c` | log ring, trace ring, symbol resolve, backtrace, object model, dump framework. |
| `kernel/tests/{scheduler,syscall,vfs,process,pci,storage,input,msi,driver,xhci,usb,hid,input_compat}_tests.c` | per-subsystem suites. |
| `kernel/tests/user_tests.c` | supervisor thread that spawns `/bin/init.hxe`. |
| `tools/verify_qemu.py` | headless QEMU boot + marker grep. |
| `tools/run_qemu.py` | interactive/`--debug`/`--headless` run (gdb stub on :1234). |
| `tools/find_ovmf.py` | locate OVMF firmware (`CODE`/`VARS`). |
| `Makefile` | `verify-*` target catalog (Prompts 2–6 + matrix). |
| `Makefile.production` | Work Unit verification targets + `verify-production-200k`. |

## Data structures / the harness contract

`ktest.h` (52 lines) defines the entire in-kernel contract:

```c
#define KT_BEGIN()              int __kt_fail = 0
#define KT_CHECK(cond, msg)     /* on !cond: print "[CHECK FAILED] msg (file:line)", set __kt_fail */
#define KT_CHECK_EQ(a, b, msg)  /* prints got/want on mismatch */
#define KT_RESULT(marker)       /* prints "[PASS] marker" or "[FAIL] marker" */
#define KT_FAILED()             (__kt_fail)
```

A suite function looks like:

```c
void debug_tests_run(void) {
    KT_BEGIN();
    KT_CHECK(strcmp(buf, "hello world\n") == 0, "log ring snapshot");
    KT_CHECK_EQ(klog_ring_used(), 12, "log ring used bytes");
    /* ... */
    KT_RESULT("debug dump tests");   /* -> "[PASS] debug dump tests" */
}
```

The marker text in `KT_RESULT` is exactly what a `verify-*` target greps for
(prefixed with `[PASS] `). Output goes through `kdprintf` / `kernel_log*`, which
write to COM1 (and the framebuffer and the in-kernel log ring).

### Boot-time test matrix (order in `kernel_main`)

| Phase | Call | Emits (selected) |
|-------|------|------------------|
| Early | `early_tests_run()` | `[TEST]`/`[PASS]` per check, `[OK] Early kernel tests passed`. Failure → `[PANIC] early test failed: <name>` + halt. |
| Work Unit A | `kernel_core_tests_run()` | `[PASS] lib list tests`, `hash table tests`, `bitmap tests`, `ring buffer tests`, `kmalloc/kfree tests`, `slab allocator tests`, `VM region tests`, `wait queue tests`, `mutex tests`, `rwlock tests`, `workqueue tests`, `timer callout tests`, `debug dump tests`; then `[OK] Kernel core production foundation online`. |
| Prompt 3 | `scheduler_tests_start()` | `[PASS] scheduler round-robin`, `sleep/wakeup`, `timer preemption`. |
| Prompt 4 | `syscall_tests_run/vfs_tests_run/process_tests_run` | `[PASS] kernel syscall dispatch`, `kernel vfs`, `kernel process table`; userland `[PASS] syscall_test`, `vfs_test`, `fd_test`, `spawn_test`. |
| Prompt 5 | `pci/storage/hnxfs/input_tests_run` | `[PASS] pci enumeration`, `block cache`, `disk read/write`, `partition parser`, `hnxfs create/write/read file`, `hnxfs mkdir`, `hnxfs unlink`, `keyboard scripted injection`, `tty canonical input`. |
| Prompt 6 | `msi/driver/xhci/usb/hid/input_compat_tests_run` | `[PASS] msi capability tests`, `driver lifecycle tests`, `xhci controller test`, `USB descriptor parser`, `usb enumeration`, `hid keyboard test`, `hid mouse test`, `ps2 keyboard still works`, `usb keyboard works`, `usb mouse works`, `tty accepts unified keyboard input`. |

`early_tests` is special: it fails *hard* (`kernel_halt_forever`) rather than
printing `[FAIL]`, because nothing later is trustworthy if boot_info, the PMM,
the VMM or the heap is broken.

### Suite entry points

Each suite is a `void <name>_tests_run(void)` function called from `kernel_main`
(directly or via an aggregator). The Work Unit A aggregator
`kernel_core_tests_run` (`kernel_core_tests.c`) calls, in order: `lib_tests_run`,
`allocator_tests_run`, `slab_tests_run`, `vm_tests_run`, `sync_tests_run`,
`workqueue_tests_run`, `timer_tests_run`, `debug_tests_run`, then prints
`[OK] Kernel core production foundation online`. Some files emit several markers
from one entry point: `lib_tests.c` emits `lib list tests`, `hash table tests`,
`bitmap tests`, `ring buffer tests`, and `lib radix/idr/strbuf tests`;
`sync_tests.c` emits `spinlock tests`, `wait queue tests`, `mutex tests`, and
`rwlock tests`; `storage_tests.c` covers both the block/disk markers and the
`hnxfs_tests_run` filesystem markers. The header `kernel_core_tests.h` exposes
the individual suites so the (reserved) test-infrastructure unit can re-run them.

### Destructive test hooks

Some tests must crash the CPU and so cannot run in a normal build. They are
compiled in only when a `MYOS_TEST_*` macro is defined via `EXTRA_KCFLAGS`
(injected by the `verify-*` target), and live in `run_destructive_tests()` in
`kernel/src/kernel.c`:

- `MYOS_TEST_INVALID_OPCODE` → `ud2` (used by `make verify-exception`).
- `MYOS_TEST_PAGE_FAULT` → dereference a non-canonical/unmapped address (used by
  `make verify-pagefault`).

`early_tests.c` also honors `MYOS_TEST_PMM_STRESS` (8×64-page PMM stress) and
`MYOS_TEST_VERBOSE` (extra diagnostics). Both `verify-exception` and
`verify-pagefault` `make clean`, build the destructive image, verify the fault
markers, then `make clean` + rebuild the normal image so the tree is restored.

### Marker conventions

`Makefile.production` codifies the marker vocabulary the verifier greps for:

- `[OK]` — a subsystem came online during boot (printed by `kernel_log_ok`).
- `[PASS]` — a named self-test passed (printed by `KT_RESULT` or directly).
- `[FAIL]` / `[PANIC]` — a self-test failed (printed by `KT_RESULT` on a failed
  `KT_CHECK`, or by the early-test/exception path); the boot may abort.

A `verify-*` target lists the `[OK]`/`[PASS]` strings it requires; their *absence*
is the failure signal. For example `make verify-hnxfs` requires
`[OK] HNXFS mounted at /disk`, `[PASS] hnxfs create file`, `[PASS] hnxfs write
file`, `[PASS] hnxfs read file`, `[PASS] hnxfs mkdir`, `[PASS] hnxfs unlink`.

### Worked example: how `verify-xhci` proves the controller works

1. `make verify-xhci` runs `make image`, then invokes `tools/verify_qemu.py`
   with six `--expect` strings.
2. QEMU boots with `-device qemu-xhci` + `usb-kbd` + `usb-mouse`.
3. `xhci_controller_setup` prints `xHCI MMIO mapped`, `xHCI command ring online`,
   `xHCI event ring online`, `xHCI controller started`; `xhci_scan_root_hub`
   prints `xHCI root hub scanned`; `xhci_init` prints `xHCI controller found`.
4. The reader thread captures all of them on COM1; the verifier removes each
   matched marker and exits 0 once the set is empty.
5. If, say, the controller failed to start, `xHCI controller started` never
   appears and the run fails with that marker listed as missing — the proof is
   that the string is emitted only after the `RUN` bit/HCH check succeeds.

## Key APIs (host verifier)

`tools/verify_qemu.py` arguments:

| Flag | Meaning |
|------|---------|
| `--image` | path to `myos.img` (required). |
| `--expect` | required serial substring; repeatable. |
| `--timeout` | seconds to wait for all markers (default 40). |
| `--mem` | QEMU `-m` value (default `256M`). |
| `--log` | captured serial log path (default `build/image/<test-name>.log`). |
| `--test-name` | label for output + log filename. |
| `--storage` / `--nvme` | disk images (auto-detected next to the image otherwise). |

It launches `qemu-system-x86_64 -machine q35` with OVMF pflash (located via
`find_ovmf.py`), the raw disk image, an AHCI `ich9-ahci` + `ide-hd` for
`storage.img`, an `nvme` device for `nvme.img`, and a `qemu-xhci` controller with
`usb-kbd` + `usb-mouse`. Serial is on stdio, display is disabled, networking is
off, `-no-reboot -no-shutdown`. A reader thread accumulates stdout; the main loop
removes satisfied markers every 0.2 s until all are found or the deadline passes.
Exit code 0 = all markers found, 1 = missing (it prints the missing markers and
the log path).

## Invariants

- **Markers come from real code.** Every `[PASS]`/`[OK]` is printed by code that
  executed and (for `[PASS]`) whose `KT_CHECK`s all held. There is no static
  pass list.
- **Failure = absence.** A failed `KT_CHECK` flips `KT_RESULT` to `[FAIL]`, so the
  `[PASS]` substring the verifier expects never appears → `make verify-*` fails.
- **Tests run pre-scheduler where possible.** The core matrix runs single-threaded
  with the kernel CR3 active, before `scheduler_start()`, so it is deterministic.
- **Normal builds are non-destructive.** Crash tests require an explicit
  `MYOS_TEST_*` macro; a default `make image` never faults on purpose.
- **The matrix runs at every memory size.** `verify-qemu-matrix` re-runs the boot
  across 128M/256M/512M/1024M/2048M.
- **Tests restore shared state.** Suites that touch the input/TTY queues drain
  them and call `tty_reset_input()` so the scripted shell session that follows is
  clean.

## Failure modes

- A missing marker → `verify_qemu.py` prints `[FAIL] <name> — missing markers:`
  with each missing string and the serial log path, exit 1.
- A boot hang (no markers, e.g. a panic before the test) → the timeout elapses
  and the same `[FAIL]` path is taken; the captured log shows the `[PANIC]` line.
- Missing tools degrade gracefully: `qemu-system-x86_64` or OVMF not found prints
  a `[DEP MISSING]` hint and exits 1 (handled by `find_ovmf.py` / the Makefile
  `need` guard).
- `early_tests` failure halts the CPU with `[PANIC] early test failed: <name>` —
  the later matrix never runs, so all of its markers go missing too.

## Verification (the make target catalog)

Run any of these from the repo root (each builds `image` first):

| Target | Asserts (selected markers) |
|--------|----------------------------|
| `make verify-boot` | kernel banner + GDT/TSS/IDT/exceptions/PMM/CR3/heap online + `Early kernel tests passed`. |
| `make verify-exception` | destructive `#UD`: `CPU EXCEPTION`, `vector ... 0x06`, `#UD Invalid Opcode`. |
| `make verify-pagefault` | destructive `#PF`: `CPU EXCEPTION`, `#PF Page Fault`, `fault addr`, `error code`. |
| `make verify-interrupts` | PIC disabled, MADT parsed, LAPIC discovered/enabled, IRQ dispatcher. |
| `make verify-timer` | PIT + LAPIC timer online, kernel tick, ticks increasing. |
| `make verify-scheduler` / `verify-preemption` | context switch, round-robin, sleep/wakeup, timer preemption. |
| `make verify-user-build` / `verify-initramfs` | all HXE programs + initramfs present (no QEMU). |
| `make verify-user-mode` / `verify-syscalls` / `verify-vfs` / `verify-process` / `verify-shell` / `verify-user-fault` | ring-3 entry, `[PASS] syscall_test`, `vfs_test`/`fd_test`, `spawn_test`, scripted shell session, isolated user fault. |
| `make verify-pci` / `verify-block` / `verify-storage` / `verify-hnxfs` | PCI scan, block cache + partition parser, AHCI disk read/write, HNXFS mount + file ops. |
| `make verify-keyboard` / `verify-tty` / `verify-expanded-userland` | PS/2 + canonical input + coreutils/storage user programs. |
| `make verify-msi` / `verify-driver-lifecycle` | PCI caps + MSI/MSI-X foundation, driver lifecycle + hw event bus. |
| `make verify-xhci` / `verify-usb` / `verify-hid` / `verify-input-unified` / `verify-hw-userland` | the USB/HID/input chain (see those docs). |
| `make verify-qemu-matrix` | `Early kernel tests passed` at 128M/256M/512M/1024M/2048M. |
| `make verify-prompt3` / `verify-prompt4` / `verify-prompt5` / `verify-prompt6` | aggregate chains for each milestone. |
| `make verify-kernel-core-expanded` | Work Unit A markers + `[OK] Kernel core production foundation online` (in `Makefile.production`). |
| `make verify-production-200k` | runs `verify-prompt6` + every expanded suite + matrix, then `production-loc-check` (≥ 200k useful LOC). |

Supporting targets: `make run` (interactive QEMU), `make debug` (gdb stub on
:1234, `gdb build/kernel/kernel.elf -ex 'target remote :1234'`), `make
run-headless`, `make inspect` (ELF headers/symbols), `make loc`,
`make production-loc-check`.

### Build-only verifications

Not every check needs QEMU. `make verify-user-build` confirms every program in
`USER_BIN` + `USER_TEST` produced a `.hxe` and the initramfs exists;
`make verify-initramfs` runs `tools/inspect_initramfs.py` to require each
`/bin/*.hxe`, `/tests/*.hxe` and `/etc/*` entry is present in the archive. These
fail fast on packaging regressions before a 90-second boot is even attempted.

### Where markers come from

The complete in-kernel marker set is the union of: the `[OK]` lines in
`kernel_main`, the early-test `[PASS]`/`[PANIC]` lines, the `KT_RESULT` markers in
the Work Unit A suites (`lib list tests`, `hash table tests`, `bitmap tests`,
`ring buffer tests`, `kmalloc/kfree tests`, `slab allocator tests`, `VM region
tests`, `wait queue tests`, `mutex tests`, `rwlock tests`, `workqueue tests`,
`timer callout tests`, `debug dump tests`), and the per-subsystem `[PASS]` lines
printed directly by the Prompt 3–6 suites. Grepping the source for `KT_RESULT(`
and `kernel_log_line("[PASS]` enumerates every marker a `verify-*` target may
reference, which keeps the Makefile expectations and the code in lockstep.

### QEMU memory matrix

`verify-qemu-matrix` (Makefile) loops:

```sh
for m in 128M 256M 512M 1024M 2048M; do
  python tools/verify_qemu.py --image build/image/myos.img --mem $m \
    --test-name matrix-$m --timeout 60 --expect "[OK] Early kernel tests passed"
done
```

This exercises the PMM/VMM/heap bring-up across a range of physical memory maps,
catching size-dependent bugs (e.g. memory-map parsing, identity-map coverage,
heap sizing). Each run writes `build/image/matrix-<mem>.log`.

## Future expansion

- **Per-test exit codes / TAP output.** Today pass/fail is inferred from marker
  presence; emitting a machine-readable summary line (count of pass/fail) would
  let CI report granular results.
- **A `verify-all` umbrella** and parallel QEMU runs to cut wall-clock time
  across the now-large matrix.
- **Fuzz / randomized suites.** `Makefile.production` already reserves
  `verify-stress` and `verify-randomized`; seeded randomized allocator/FS/IO
  stress would catch ordering bugs the deterministic suites miss.
- **Coverage instrumentation** (which functions a boot exercised) to find
  untested paths as the LOC grows toward the roadmap targets.
- **Userland test framework.** As more runs in ring 3, a userland harness
  (process spawning + assertions over syscalls) would complement the in-kernel
  matrix.
- **KVM acceleration in CI** to make the multi-size matrix and stress runs fast
  enough to run on every commit.
- **Marker linting.** A small tool that cross-checks every `--expect` string in
  the Makefiles against the `KT_RESULT`/`kernel_log_*` strings in the source would
  catch a renamed marker before a verify run wastes a 90-second boot.
- **Timeout tuning per target.** The fixed `--timeout` (60–90 s) is conservative;
  per-target budgets plus early-exit on the first `[PANIC]`/`[FAIL]` line would
  shorten failing runs.
- **Golden serial logs.** Diffing a captured boot log against a known-good
  baseline (beyond marker presence) would catch unexpected warnings and ordering
  regressions that marker-grep alone misses.
