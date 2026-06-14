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

## Prompt 4 — userland foundation mega-phase

A full first user/kernel + userland layer: ring 3, syscalls, a real VFS with
ramfs/devfs, a process model (spawn/wait/exit), file-descriptor tables, a TTY
+ console, an init process, a scripted shell, 15 coreutils, and 5 user tests.

| Subsystem | Where |
|-----------|-------|
| Bootloader initramfs load | `bootloader/src/efi_main.c`, `boot_info.initramfs_*` |
| HXF1 initramfs format + packer | `tools/mkinitramfs.py`, `tools/inspect_initramfs.py` |
| Kernel initramfs parser | `kernel/initramfs/initramfs.{c,h}` |
| HXE1 user executable + loader | `kernel/user/user_loader.{c,h}`, `tools/mkhxe.py`, `tools/inspect_hxe.py` |
| User runtime (no libc) | `user/include/*`, `user/lib/*` |
| User address spaces | `kernel/user/user_address_space.{c,h}` |
| Ring 3 transition | `kernel/user/user_entry.S`, `user.{c,h}`, `tss_set_rsp0` |
| Syscall entry / table / dispatch | `kernel/user/syscall_entry.S`, `syscall_table.{c,h}`, `syscall.{c,h}` |
| User pointer validation / copy | `kernel/user/user_copy.{c,h}` |
| File-descriptor table | `kernel/process/fd_table.{c,h}` |
| VFS core | `kernel/fs/{vfs,inode,file,path}.{c,h}` |
| ramfs (initramfs view) | `kernel/fs/ramfs/ramfs.{c,h}` |
| devfs | `kernel/fs/devfs/devfs.{c,h}` |
| Device registry / char devices | `kernel/device/{device,char_device}.{c,h}` |
| Console + TTY v0 | `kernel/tty/{console,tty}.{c,h}` |
| Process / task model v0 | `kernel/process/{process,process_table}.{c,h}` |
| spawn / exec / wait / exit | `kernel/process/{exec,wait}.{c,h}`, `process.c` |
| User fault isolation | `kernel/user/user_fault.{c,h}`, `exceptions.c` |
| init / shell / coreutils / tests | `user/init`, `user/shell`, `user/coreutils`, `user/tests` |
| Kernel-side unit tests | `kernel/tests/{syscall,vfs,process,user}_tests.{c,h}` |

### HXF1 initramfs format
Custom read-only archive (not tar/cpio/zip): `struct hxf_header` (magic
`0x31465848`, version, entry_count, header_size) + `struct hxf_entry`
(`char path[128]`, offset, size, flags) × N + 8-byte-aligned blobs. The
bootloader loads `\boot\initramfs.hxf` as `EfiLoaderData`; the kernel parses it
in place (identity-mapped, never freed) and ramfs builds a directory tree over
it. Packed by `tools/mkinitramfs.py`, inspected by `tools/inspect_initramfs.py`.

### HXE1 user executable format
The kernel never parses ELF in ring 0. `struct hxe_header` (magic `0x31455848`,
version, entry, segment_count, header_size) + `struct hxe_segment` (vaddr,
memsz, filesz, file_offset, R/W/X flags) × N + segment bytes. `tools/mkhxe.py`
flattens a linked user ELF's PT_LOAD program headers into HXE1.

### User address space layout
`USER_IMAGE_BASE=0x400000`, `USER_HEAP_BASE=0x4000000000` (1 MiB pre-mapped),
`USER_STACK_TOP=0x7FFFFFFFE000`, `USER_STACK_SIZE=0x40000`,
`USER_TOP=0x800000000000`. Each process PML4 mirrors the kernel low footprint
`[0, 4 MiB)`, the framebuffer, the LAPIC MMIO, and the initramfs RAM as
supervisor pages (so kernel code and ramfs reads work under any user CR3), plus
the user image / heap / stack as user pages.

### CR3 discipline (key invariant)
A syscall runs with the calling process's CR3 active, so user **data** is reached
directly at its virtual address — but the user CR3 only mirrors `[0, 4 MiB)`, so
the user's *page-table pages* and high data frames are not addressable by
physical (identity) address under it. Therefore:
* User-memory copies (`user_copy_*`) and validation switch to the **kernel CR3**
  and access user memory by walking the process PML4 + identity-mapped physical
  frames. Non-blocking window, so holding kernel CR3 across it is safe.
* Process creation (page-table allocation, image copy) and reaping (page-table
  teardown) also run with the kernel CR3 active (`process_spawn_argv`, `wait.c`).

### int 0x80 syscall ABI
`rax`=number; args `rdi, rsi, rdx, r10, r8, r9`; result in `rax`; negative =
`-errno`. Vector `0x80` is a DPL-3 interrupt gate. Invalid number → `-38`
(`-ENOSYS`). Calls: exit, write, read, sleep, getpid, yield, open, close,
lseek, readdir, spawn, wait, getcwd, chdir, uptime, meminfo, ps. A static table
(`syscall_table.c`) maps numbers to handlers; every user pointer is validated in
software and the kernel never panics on bad user input.

### VFS design
`struct vnode` (file / dir / chardev + `vnode_ops`), `struct file` (refcounted
vnode + offset + flags), `struct filesystem` (name + `lookup`), a small mount
table, and path normalization (`path_resolve`). `vfs_resolve` finds the longest
matching mount prefix and delegates the remainder to that filesystem.
fd-based ops act on the current process's fd table. Mounts: ramfs at `/`,
devfs at `/dev`.

### devfs design
Synthesizes `/dev` from the character-device registry: `/dev/console`
(framebuffer+serial out, scripted TTY in), `/dev/null` (read EOF / write
sink), `/dev/zero` (read zeroes / write sink).

### FD table
Per-process fixed array of 32 open-file pointers; `fd_alloc` (lowest-free),
`fd_close`, `fd_get`, `fd_install_at`. fds 0/1/2 are wired to a shared,
refcounted `/dev/console` open file at process creation.

### Process model v0
`struct process` { pid, parent_pid, name, state, exit_code, address_space,
main_thread, fds, cwd, entry_rip, user_rsp }. One kernel thread per process,
bound via `thread->proc`. `process_spawn[_argv]` loads HXE1 from the VFS, builds
the address space + argv stack + fd table, creates the thread, and admits it.
`process_wait` polls a child to termination, copies its exit code, and reaps it
(address space + fds freed). `process_exit_current` / `process_fault_current`
mark the process and reschedule. The process table is a 64-slot array with
monotonic PIDs, interrupt-safe alloc/free.

### TTY v0 + scripted shell
No keyboard yet (Prompt 5). The TTY keeps a scripted input buffer that
`/dev/console` serves to readers until exhausted (then EOF). The kernel
pre-loads a shell command script; `/bin/shell.hxe` drains stdin, executes each
line (builtins `cd`/`exit` in-process, everything else spawned as
`/bin/<cmd>.hxe`), and prints `[PASS] shell scripted session`.

### init + userland flow
`/bin/init.hxe` (PID 1) prints the banner, runs `/tests/{syscall,fd,vfs,spawn,
fault}_test.hxe`, launches the scripted shell, and exits 0. The fault test
deliberately dereferences an unmapped ring-3 address; the kernel prints
`[OK] User fault isolated` and continues. The kernel supervisor reaps init and
prints `[OK] Userland foundation tests passed`.

### coreutils
`echo cat ls pwd clear help true false yes whoami uptime meminfo ps testread
hello` — each a standalone HXE1 program under `/bin`.

### User fault isolation
`x86_exception_dispatch` routes any fault with `CS.RPL == 3` to
`user_fault_handle`, which prints a diagnostic, logs `[OK] User fault isolated`,
marks the process `FAULTED`, and reschedules. CPL-0 faults keep the fatal path.

### Verification (Prompt 4)
| Target | Asserts |
|--------|---------|
| `make verify-user-build` | every `/bin` + `/tests` HXE and the initramfs exist |
| `make verify-initramfs` | the archive contains all required `/bin`, `/tests`, `/etc` files |
| `make verify-user-mode` | `[OK] Ring 3 entry online`, `[USER] hello from ring 3` |
| `make verify-syscalls` | `[PASS] syscall_test` |
| `make verify-vfs` | `[PASS] vfs_test`, `[PASS] fd_test` |
| `make verify-process` | `[PASS] spawn_test` |
| `make verify-shell` | `[PASS] shell scripted session` |
| `make verify-user-fault` | `[OK] User fault isolated` + `[OK] Userland foundation tests passed` |
| `make verify-prompt4` | all Prompt 3 targets + all of the above |

## Prompt 5 — storage and device expansion mega-phase

The first serious hardware/storage/input layer (`MyOS Kernel 0.0.5`).

| Subsystem | Where |
|-----------|-------|
| Driver core | `kernel/driver/{driver,device_id,driver_registry}.{c,h}` |
| PCI (CF8/CFC) | `kernel/pci/{pci,pci_config,pci_device,pci_ids,pci_driver}.{c,h}` |
| Block + write-through cache | `kernel/block/{block_device,block_request,block_cache,block_registry}.{c,h}` |
| Partition (MBR/GPT) | `kernel/partition/{mbr,gpt,partition}.{c,h}` |
| AHCI (SATA) | `kernel/storage/ahci/{ahci,ahci_controller,ahci_port,ahci_command,ahci_disk}.{c,h}` |
| NVMe foundation | `kernel/storage/nvme/{nvme,nvme_controller,nvme_queue,nvme_namespace,nvme_block}.{c,h}` |
| HNXFS persistent FS | `kernel/fs/hnxfs/{hnxfs,hnxfs_format,hnxfs_inode,hnxfs_dir,hnxfs_alloc,hnxfs_file}.*` |
| I/O APIC routing | `kernel/arch/x86_64/ioapic.{c,h}` |
| PS/2 + keyboard | `kernel/input/{input_event,input_queue}.* + ps2/* + keyboard/*` |
| Canonical TTY | `kernel/tty/tty.c` (line discipline) |
| VFS expansion | `vfs_mkdir/create/unlink/stat + mount introspection`, syscalls 17–22 |
| PMM contiguous alloc | `pmm_alloc_contig` (DMA) |
| Thread stack reuse | `thread_reap` (bounds kernel heap across many spawns) |
| Tools | `tools/disk/*`, `tools/fs/*`, `tools/pci/pci_ids_min.py` |

New syscalls: `SYS_MKDIR 17`, `SYS_UNLINK 18`, `SYS_STAT 19`,
`SYS_MOUNT_INFO 20`, `SYS_DEVICES 21`, `SYS_BLOCKS 22`.

Details in [prompt5.md](prompt5.md), [pci.md](pci.md), [block.md](block.md),
[storage.md](storage.md), [hnxfs.md](hnxfs.md), [input.md](input.md),
[tty.md](tty.md).

### Verification (Prompt 5)
| Target | Asserts |
|--------|---------|
| `verify-pci` | PCI bus scanned; pci enumeration |
| `verify-block` | block layer online; block cache; partition parser |
| `verify-storage` | AHCI block device online; disk read; disk write |
| `verify-hnxfs` | HNXFS mounted; create/write/read/mkdir/unlink |
| `verify-keyboard` | PS/2 controller; keyboard input; scripted injection |
| `verify-tty` | TTY interactive input; canonical input; shell interactive smoke |
| `verify-expanded-userland` | expanded coreutils; storage user programs |
| `verify-prompt5` | verify-prompt4 + all of the above + verify-qemu-matrix |

AHCI read/write works under QEMU; HNXFS persists through the write-through cache
to `storage.img`; NVMe is discovered + inspected with block I/O deferred.

## Next milestone (after Prompt 5)
**Prompt 6 — USB and hardware compatibility mega-phase:** xHCI controller, USB
device enumeration, USB HID keyboard/mouse, improved input stack, PCI MSI/MSI-X
foundation, driver power/reset handling, broader hardware compatibility, and
expanded interactive userland.
