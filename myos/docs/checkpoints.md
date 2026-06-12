# Checkpoints

## Prompt 1 — boot pipeline (stages 1–16)

| # | Stage | Where | Done |
|---|-------|-------|------|
| 1 | Project folder structure | whole tree | ✅ |
| 2 | Build system | `Makefile` | ✅ |
| 3 | Cross-compilation setup | clang targets in `Makefile` | ✅ |
| 4 | Linker script | `kernel/arch/x86_64/linker.ld` | ✅ |
| 5 | QEMU boot test setup | `tools/run_qemu.py` | ✅ |
| 6 | UEFI disk image builder | `tools/build_image.py` | ✅ |
| 7 | UEFI bootloader entry point | `bootloader/src/efi_main.c` | ✅ |
| 8 | UEFI screen/log output | `bootloader/src/log.c` | ✅ |
| 9 | UEFI file loading | `bootloader/src/file.c` | ✅ |
| 10 | Kernel ELF loader | `bootloader/src/elf_loader.c` | ✅ |
| 11 | Boot information structure | `shared/include/boot_info.h` | ✅ |
| 12 | UEFI memory map reader | `bootloader/src/exit_boot.c` | ✅ |
| 13 | UEFI framebuffer/GOP reader | `bootloader/src/framebuffer.c` | ✅ |
| 14 | ACPI RSDP finder | `bootloader/src/acpi.c` | ✅ |
| 15 | ExitBootServices logic | `bootloader/src/exit_boot.c` | ✅ |
| 16 | Kernel jump logic | `bootloader/src/jump.c` | ✅ |

## Prompt 2 — kernel CPU + memory foundation (stages 17–34)

| # | Stage | Where | Done |
|---|-------|-------|------|
| 17 | Kernel arch folder expansion | `kernel/arch/x86_64/*`, `kernel/memory/*`, `kernel/tests/*` | ✅ |
| 18 | CPU type/register definitions | `kernel/arch/x86_64/cpu.{c,h}`, `cr.S`, `port_io.S` | ✅ |
| 19 | Early serial COM1 logging | `kernel/arch/x86_64/serial.{c,h}` | ✅ |
| 20 | Kernel logging router | `kernel/src/log.c`, `kernel/include/log.h` | ✅ |
| 21 | GDT setup | `kernel/arch/x86_64/gdt.{c,h}`, `gdt_load.S` | ✅ |
| 22 | TSS setup | `kernel/arch/x86_64/tss.{c,h}` | ✅ |
| 23 | IDT setup | `kernel/arch/x86_64/idt.{c,h}` | ✅ |
| 24 | Exception assembly stubs | `kernel/arch/x86_64/isr.S` | ✅ |
| 25 | C exception dispatcher | `kernel/arch/x86_64/exceptions.{c,h}` | ✅ |
| 26 | Page fault handler | `kernel/arch/x86_64/exceptions.c` (`page_fault_dump`) | ✅ |
| 27 | CPU halt + panic hardening | `kernel/src/panic.c`, `panic.h`, `halt.S` | ✅ |
| 28 | Physical memory map parser | `kernel/memory/pmm.c` | ✅ |
| 29 | Physical page allocator | `kernel/memory/pmm.{c,h}` (bitmap) | ✅ |
| 30 | Kernel virtual memory constants | `kernel/memory/memory_layout.h` | ✅ |
| 31 | Kernel page table builder | `kernel/memory/vmm.c`, `kernel/arch/x86_64/paging.{c,h}` | ✅ |
| 32 | Kernel heap allocator | `kernel/memory/heap.{c,h}` (bump) | ✅ |
| 33 | Early kernel test framework | `kernel/tests/early_tests.{c,h}` | ✅ |
| 34 | Documentation + validation | `docs/*`, `README.md` | ✅ |

## PMM design
- Bitmap allocator, 1 bit per 4 KiB page (set = used/reserved).
- The bitmap is sized to the highest **RAM** address (a positive type whitelist
  excludes reserved/MMIO/unusable regions, e.g. the 64-bit PCI hole, so the
  bitmap stays small).
- Start fully used → free every `EfiConventionalMemory` region → re-reserve
  page 0, the kernel image, `boot_info`, the UEFI memory map, and the bitmap.
- Framebuffer / ACPI / UEFI-runtime ranges are non-conventional and are never
  freed, so they remain reserved automatically.
- `pmm_free_page` rejects misaligned, out-of-range, and double frees.

## VMM design
- Kernel-owned PML4 built from PMM pages.
- **Identity map** of all RAM (+ low 4 GiB floor) with 2 MiB pages; the
  framebuffer range is mapped cache-disabled/write-through. This single mapping
  satisfies every required mapping (kernel code/data/bss, stack, framebuffer,
  `boot_info`, UEFI map, PMM bitmap, low identity for the running code).
- Kernel image additionally mapped into the higher half
  (`KERNEL_HIGHER_HALF_BASE`) for future use.
- `vmm_map_page` / `vmm_unmap_page` / `vmm_get_physical` operate on the kernel
  PML4 with 4 KiB pages and `invlpg`.

## Custom CR3
**Loaded.** `vmm_load_kernel_address_space()` activates the kernel page tables
and the boot prints `[OK] Kernel CR3 loaded` (verified: `cr3 = 0x1000`). The
identity map guarantees the running code, stack, and all data structures remain
addressable across the switch.

## Heap
Early **bump allocator** over a 2 MiB static `.bss` arena (no VMM mapping
needed — it is part of the identity/higher-half-mapped kernel image). `kfree`
is a documented no-op for now.

## Acceptance
`make clean && make all && make image && make run` boots and prints, via both
the framebuffer console and COM1 serial:

```
MyOS Kernel 0.0.2
[OK] Kernel entered
[OK] Boot info magic valid
[OK] Framebuffer console online
[OK] Serial online
[OK] Kernel logger online
[OK] CPU state readable
[OK] GDT loaded
[OK] TSS loaded
[OK] IDT loaded
[OK] Exceptions online
[OK] Physical memory map parsed
[OK] Physical memory manager online
[OK] Page allocator online
[OK] Kernel page tables built
[OK] Kernel CR3 loaded
[OK] Page fault handler online
[OK] Kernel heap online
[OK] Early kernel tests passed
```

## Prompt 2.5 — hardening, verification, cleanup

Correction/validation pass (no new subsystems). Changes:

- **Repo hygiene:** root `.gitignore`; all `build/` artifacts and
  `tools/__pycache__/` untracked (only `.gitkeep` remains tracked).
- **Verification targets:** `make inspect`, `make loc`, `make run-headless`,
  `make verify-boot`, `make verify-exception`, `make verify-pagefault`,
  `make verify-qemu-matrix`, backed by `tools/verify_qemu.py` (headless serial
  capture, no GUI required).
- **ISR trap frame audited:** in x86-64 long mode the CPU pushes SS:RSP
  *unconditionally* (unlike 32-bit, which only pushes on a CPL change), so the
  frame already carries the correct interrupted RSP/SS for kernel-mode faults.
  Confirmed live: `make verify-exception` (#UD) and `make verify-pagefault`
  (#PF) show `cs=0x08`, `ss=0x10`, and an `rsp` inside the kernel stack. The
  dispatcher also reports CPL/mode and normalizes a NULL SS for display.
- **PMM hardened:** reserves all memory below 1 MiB
  (`LOW_MEMORY_RESERVED_END`), never allocates below it, extra diagnostics
  (reserved pages, bitmap base/size, lowest allocatable, highest RAM), and a
  64-page stress test (alignment / uniqueness / free-count restoration).
- **VMM/CR3 validated:** after loading CR3, `vmm_validate_required_mappings()`
  reads CR3 back, confirms it equals the kernel PML4, translates kernel /
  framebuffer / boot_info / UEFI map / PMM bitmap, and round-trips a scratch
  page. Boot prints `[OK] VMM required mappings validated`.
- **Framebuffer pixel format:** `fbcon_pack_color()` derives channel shifts
  from the GOP masks (RGB / BGR / BitMask); unrecognized formats warn
  `[WARN] framebuffer pixel format fallback`.
- **Destructive test flags:** `MYOS_TEST_INVALID_OPCODE`, `MYOS_TEST_PAGE_FAULT`,
  `MYOS_TEST_PMM_STRESS`, `MYOS_TEST_VERBOSE` — never enabled in a normal build.

### Verification status

| Capability | Status |
|------------|--------|
| Boot → ExitBootServices → kernel framebuffer | implemented, tested by normal boot (`make verify-boot`) |
| Serial COM1 + logging router | implemented, tested by normal boot |
| GDT / TSS / IDT install | implemented, tested by normal boot (sgdt/sidt checks) |
| CPU exception dispatch (#UD) | implemented, tested by `make verify-exception` |
| Page-fault handler + CR2 decode | implemented, tested by `make verify-pagefault` |
| PMM bitmap alloc/free + stress | implemented, tested by normal boot + matrix |
| VMM kernel page tables + CR3 | implemented & **loaded**, validated by normal boot |
| Multi-RAM-size boot | tested by `make verify-qemu-matrix` (128M–2048M) |
| Heap (bump allocator, `kfree` no-op) | implemented, tested by normal boot |
| Interrupts/timer/scheduler/userland | **not yet implemented** (Prompt 3+) |
| `kfree` reclamation, slab/free-list | **intentionally deferred** |

## Prompt 3 — interrupt controller + scheduler foundation (stages 35–54)

| # | Stage | Where | Done |
|---|-------|-------|------|
| 35 | Interrupt subsystem structure | `kernel/arch/x86_64/irq.{c,h}` | ✅ |
| 36 | Legacy PIC disable hardening | `kernel/arch/x86_64/pic.{c,h}` | ✅ |
| 37 | ACPI MADT parser | `kernel/arch/x86_64/madt.{c,h}` | ✅ |
| 38 | Local APIC discovery | `kernel/arch/x86_64/apic.c` (`lapic_discover`) | ✅ |
| 39 | Local APIC enable | `kernel/arch/x86_64/apic.c` (`lapic_enable`) | ✅ |
| 40 | Local APIC EOI | `kernel/arch/x86_64/apic.c` (`lapic_send_eoi`) | ✅ |
| 41 | IRQ dispatcher | `kernel/arch/x86_64/irq.c` | ✅ |
| 42 | IRQ assembly stubs | `kernel/arch/x86_64/irq_stubs.S` | ✅ |
| 43 | PIT timer driver | `kernel/arch/x86_64/pit.{c,h}` | ✅ |
| 44 | Local APIC timer driver | `kernel/arch/x86_64/lapic_timer.{c,h}` | ✅ |
| 45 | Timer abstraction | `kernel/arch/x86_64/timer.{c,h}` | ✅ |
| 46 | Kernel tick counter | `kernel/arch/x86_64/timer.c` (`kernel_ticks`) | ✅ |
| 47 | Kernel thread structure | `kernel/sched/thread.{c,h}` | ✅ |
| 48 | Context switch assembly | `kernel/sched/context_switch.S` | ✅ |
| 49 | Ready queue | `kernel/sched/scheduler.c` (FIFO) | ✅ |
| 50 | Round-robin scheduler | `kernel/sched/scheduler.c` | ✅ |
| 51 | Idle thread | `kernel/sched/idle.{c,h}` | ✅ |
| 52 | Sleep/wakeup system | `kernel/sched/sleep.{c,h}` | ✅ |
| 53 | Timer preemption | `scheduler_on_timer_tick` + `scheduler_irq_exit` | ✅ |
| 54 | Scheduler tests + verification | `kernel/tests/scheduler_tests.c`, `make verify-*` | ✅ |

### IRQ architecture
- Hardware IRQs are fully separate from CPU exceptions: dedicated stubs
  (`irq_stubs.S`) for vectors 0x20–0x2F, 0x30 (LAPIC timer), 0xF0 (spurious),
  installed as ring-0 interrupt gates (IF=0 inside handlers — no nesting).
- `irq_dispatch` counts every vector, calls the registered handler, warns once
  on unregistered vectors, sends the LAPIC EOI, then runs the deferred
  preemption switch (`scheduler_irq_exit`). Spurious (0xF0) gets no EOI.
- EOI is deliberately sent **before** any preemption context switch so a
  switched-away thread can never block the next timer interrupt.

### PIC/APIC design
- The 8259 PIC is remapped to 0x20/0x28 (so stray IRQs can't corrupt
  exception vectors) and fully masked at boot: `[OK] Legacy PIC disabled`.
- The LAPIC base comes from MSR `0x1B` cross-checked with the MADT; its 2 MiB
  identity page is remapped cache-disable/write-through (`vmm_map_mmio_2m`).
- LAPIC enabled via spurious-vector register (`0xF0 | enable`), TPR=0.

### MADT parsing
RSDP (rev ≥ 2 → XSDT, else RSDT, both checksummed) → `APIC` table → entries
0 (CPU count), 1 (I/O APIC base/GSI, recorded but not yet programmed),
2 (IRQ0 override), 4 (LAPIC NMI, ignored), 5 (64-bit LAPIC override).
Packed fields are read with `memcpy` (alignment-safe), entry bounds validated.

### Timer source
**The LAPIC timer drives scheduling** (periodic, vector 0x30, 100 Hz,
divide-by-16), calibrated against a 50 ms PIT polled delay (channel-0 down
counter, wrap-safe delta accumulation — no PIT interrupt needed). The PIT
fallback path (re-enable PIC for IRQ0 only + PIT periodic) exists but is not
exercised in QEMU; if taken it logs
`[WARN] Local APIC timer unavailable, using PIT scheduler timer`.
Note: PIT interrupts are not routed in the normal path (PIC masked, I/O APIC
not yet programmed) — the PIT is calibration/polling only.

### Thread model & context switch
- Kernel threads only; 16 KiB heap-allocated stacks; states
  NEW/READY/RUNNING/SLEEPING/BLOCKED/DEAD.
- `context_switch(old_rsp*, new_rsp)` saves rbp/rbx/r12–r15 + RSP (SysV
  callee-saved set; caller-saved regs are dead across a call, and a preempted
  thread's full GPR state lives in its `irq_stubs.S` frame).
- New threads start in `thread_trampoline` (planted as the return slot of a
  hand-built initial frame, 16-byte-aligned), which does `sti`, runs
  `entry(arg)`, then `thread_exit()`.

### Scheduler design
- Single-core round robin, FIFO ready queue, 5-tick (50 ms) quantum, no
  priorities. The idle thread is never enqueued — it is the fallback when the
  queue is empty. The boot context is abandoned at `scheduler_start()`.
- Preemption: the tick handler (IRQ context) wakes sleepers and decrements the
  quantum; on expiry it sets `need_resched`, and the actual switch happens in
  `scheduler_irq_exit()` after EOI. A preempted thread resumes later through
  its interrupted IRQ frame → `iretq`, with no state loss.
- The whole kernel is compiled `-mno-sse -mno-mmx -msoft-float` because the
  IRQ stubs save only GPRs.

### Verification (Prompt 3)
| Target | Asserts |
|--------|---------|
| `make verify-interrupts` | PIC disabled, MADT parsed, LAPIC discovered/enabled, IRQ dispatcher online |
| `make verify-timer` | PIT online, LAPIC timer online, kernel tick online, ticks observed increasing |
| `make verify-scheduler` | context switch/scheduler/sleep-wakeup online, round-robin + sleep/wakeup tests pass |
| `make verify-preemption` | preemption online + `[PASS] timer preemption` (quantum-expiry switch observed) |
| `make verify-prompt3` | all of the above + boot + #UD + #PF |

Scheduler self-tests run on every boot: threads A (yield churn), B (20 ms
sleep loop), C (8-tick busy spin per round → forced quantum preemption), and a
checker that validates counters, switch/preempt/wakeup counts and tick
progress, then prints the `[TEST]`/`[PASS]` protocol.

## Prompt 4 — user/kernel boundary (stages 55–80)

| # | Stage | Where | Done |
|---|-------|-------|------|
| 55 | Bootloader initramfs loading | `bootloader/src/efi_main.c` | ✅ |
| 56 | Initramfs archive format + packer | `tools/mkinitramfs.py` (HXF1) | ✅ |
| 57 | Kernel initramfs parser | `kernel/initramfs/initramfs.{c,h}` | ✅ |
| 58 | User executable format | `kernel/user/user_loader.h` (HXE1) | ✅ |
| 59 | User program build system | `Makefile` (`make user`), `tools/mkhxe.py` | ✅ |
| 60 | User-space runtime layout | `user/lib/*`, `user/linker.ld` | ✅ |
| 61 | User address space creation | `kernel/user/user_address_space.c` | ✅ |
| 62 | User page mapping helpers | `user_map_page/range`, `user_copy_to_space` | ✅ |
| 63 | Ring 3 GDT/TSS validation | `kernel/user/user.c`, `tss_set_rsp0` | ✅ |
| 64 | User-mode entry path | `kernel/user/user_entry.S` | ✅ |
| 65 | Syscall vector setup | `kernel/user/syscall.c` (`int 0x80`, DPL 3) | ✅ |
| 66 | Syscall assembly entry | `kernel/user/syscall_entry.S` | ✅ |
| 67 | Syscall dispatcher | `kernel/user/syscall.c` (`syscall_dispatch`) | ✅ |
| 68 | Basic syscall table | `kernel/user/syscall_numbers.h` | ✅ |
| 69 | write syscall | `sys_write` (fd 1/2, validated copy) | ✅ |
| 70 | exit syscall | `sys_exit` -> `user_task_exit_current` | ✅ |
| 71 | yield syscall | `sys_yield` -> `scheduler_yield` | ✅ |
| 72 | sleep syscall | `sys_sleep` -> `thread_sleep_ms` | ✅ |
| 73 | getpid syscall | `sys_getpid` (user task id) | ✅ |
| 74 | read syscall stub | `sys_read` (fd 0 EOF / len 0 -> 0) | ✅ |
| 75 | User task/thread integration | `kernel/user/user_task.c`, scheduler CR3/RSP0 | ✅ |
| 76 | User fault handling | `kernel/user/user_fault.c`, `exceptions.c` | ✅ |
| 77 | First user program | `user/init/init.c` | ✅ |
| 78 | User syscall tests | `user/tests/syscall_test.c`, `kernel/tests/user_tests.c` | ✅ |
| 79 | Verification targets | `Makefile` (`verify-user-*`, `verify-prompt4`) | ✅ |
| 80 | Documentation | `README.md`, `docs/*` | ✅ |

### HXF1 initramfs format
Custom read-only archive (not tar/cpio/zip): `struct hxf_header` (magic
`0x31465848`, version, entry_count, header_size) + `struct hxf_entry`
(`char path[128]`, offset, size, flags) × N + blobs. The bootloader loads
`\boot\initramfs.hxf` as `EfiLoaderData`; the kernel parses it in place
(identity-mapped, never freed). Packed by `tools/mkinitramfs.py`.

### HXE1 user executable format
Custom load format (the kernel does **not** parse ELF in ring 0):
`struct hxe_header` (magic `0x31455848`, version, entry, segment_count,
header_size) + `struct hxe_segment` (vaddr, memsz, filesz, file_offset, flags
R/W/X) × N + segment bytes. `tools/mkhxe.py` flattens a linked user ELF's
PT_LOAD program headers into HXE1 (file_size ≤ memory_size, addresses below
`USER_TOP` and ≥ `USER_IMAGE_BASE`, no overlap, entry inside an exec segment).

### User address space layout
`USER_IMAGE_BASE=0x400000`, `USER_STACK_TOP=0x7FFFFFFFE000`,
`USER_STACK_SIZE=0x20000`, `USER_TOP=0x800000000000`. Each task's PML4 mirrors
the kernel low footprint `[0, 4 MiB)`, the framebuffer, and the LAPIC MMIO as
supervisor pages (so kernel handlers run under any user CR3), plus user image +
stack as user pages. The image base sits at the 2 MiB boundary above the kernel
image, so kernel and user pages never overlap. `kfree`/page reclamation in
`user_address_space_destroy` frees only user-flagged leaves and the per-space
tables.

### int 0x80 syscall ABI
`rax`=number; args `rdi, rsi, rdx, r10, r8, r9`; result in `rax`; negative =
error. Vector `0x80` is a DPL-3 interrupt gate. Invalid number → `-38`
(`-ENOSYS`); bad user pointer → `-14` (`-EFAULT`); the kernel validates user
pointers in software and never panics on bad user input.

### read syscall limitation
There is no console input device in Prompt 4: `read(0, …)` returns `0` (EOF) and
any `read` with length `0` returns `0`. A real device/TTY-backed read arrives in
Prompt 5.

### User fault isolation
`x86_exception_dispatch` routes any fault with `CS.RPL == 3` to
`user_fault_handle`, which prints a diagnostic, logs `[OK] User fault isolated`,
marks the task `FAULTED`, and reschedules — the kernel never panics on a user
fault. CPL-0 faults keep the original fatal path.

### Verification (Prompt 4)
| Target | Asserts |
|--------|---------|
| `make verify-user-build` | `init.hxe`, `syscall_test.hxe`, `initramfs.hxf` exist |
| `make verify-initramfs` | initramfs file loaded (bootloader) + loaded/parsed (kernel) |
| `make verify-user-mode` | ring 3 entry online; first user program exits cleanly |
| `make verify-syscalls` | every `[USER] … OK` marker + `User/kernel boundary tests passed` |
| `make verify-user-fault` | a deliberate ring-3 #PF is isolated; kernel keeps running |
| `make verify-prompt4` | all Prompt 3 targets + all of the above |

## Next milestone (after Prompt 4)
**Prompt 5 — process model and interactive userland:** real process table,
spawn/exec/wait, file-descriptor model, devfs console, keyboard input, TTY
layer, init process, shell, and first core utilities.
