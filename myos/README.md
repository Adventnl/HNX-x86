# MyOS — x86-64 UEFI boot pipeline

A from-scratch x86-64 UEFI operating-system boot pipeline (stages 1–16): a
custom `BOOTX64.EFI` UEFI application that loads an ELF64 kernel, hands it a
`struct boot_info`, exits UEFI boot services, and jumps to the kernel, which
prints to the screen via its own framebuffer console.

**Locked architecture:** x86-64 · UEFI · custom `BOOTX64.EFI` · custom minimal
UEFI headers (no GRUB / Limine / gnu-efi / EDK2) · bootloader is PE/COFF ·
kernel is ELF64 · QEMU + OVMF · kernel loaded from `\boot\kernel.elf`.

## Layout
```
myos/
├── Makefile
├── docs/            boot_process / boot_protocol / build / checkpoints
├── shared/include/  boot_info.h           (shared bootloader↔kernel ABI)
├── bootloader/      custom UEFI headers + EFI application sources
├── kernel/          ELF64 kernel
│   ├── initramfs/   HXF1 archive parser
│   └── user/        ring-3 entry, syscalls, user address spaces, HXE1 loader
├── user/            ring-3 programs (crt0 + tiny libc, init, syscall_test)
├── tools/           build_image.py / find_ovmf.py / run_qemu.py / mkhxe.py / mkinitramfs.py
└── build/           generated artifacts
```

## Quick start
```bash
make all         # build BOOTX64.EFI + kernel.elf
make user        # build the ring-3 programs (HXE1)
make initramfs   # pack build/image/initramfs.hxf (HXF1)
make image       # build build/image/myos.img (FAT, includes the initramfs)
make run         # boot in QEMU + OVMF
```

See [docs/build.md](docs/build.md) for host dependencies and
[docs/boot_process.md](docs/boot_process.md) for the full boot flow.

## Disk image
FAT, containing:
```
EFI/BOOT/BOOTX64.EFI
boot/kernel.elf
boot/initramfs.hxf
```

## Scope
Implemented:
- **Prompt 1 (stages 1–16):** firmware → bootloader → ELF kernel → framebuffer text.
- **Prompt 2 (stages 17–34):** kernel CPU + memory foundation — serial COM1
  logging, logging router, CPU/register helpers, GDT + TSS, IDT + exception
  handlers, page-fault diagnostics, bitmap physical memory manager, kernel page
  tables with a **loaded custom CR3**, an early kernel heap, and an early
  self-test framework. Boots to `MyOS Kernel 0.0.2`.
- **Prompt 2.5 (hardening/verification):** repo cleanup + `.gitignore`,
  headless verification targets, audited ISR trap frame, low-1 MiB PMM
  reservation + diagnostics + stress test, CR3 read-back/mapping validation,
  pixel-format-aware framebuffer packing, and opt-in destructive test flags.
- **Prompt 3 (stages 35–54):** interrupt + scheduler foundation — legacy PIC
  disabled, ACPI MADT parsed, Local APIC enabled, dedicated IRQ
  dispatcher/stubs, PIT-calibrated **LAPIC timer at 100 Hz**, kernel tick,
  kernel threads with assembly context switch, FIFO round-robin scheduler
  (50 ms quantum), idle thread, tick-based sleep/wakeup, and timer preemption
  — all proven by on-boot scheduler self-tests. Boots to `MyOS Kernel 0.0.3`.
- **Prompt 4 (stages 55–80):** user/kernel boundary — bootloader loads a custom
  **HXF1 initramfs**; the kernel parses it and loads a custom **HXE1** user
  executable into a per-task **user address space** (private PML4 mirroring the
  kernel footprint + framebuffer + LAPIC). Ring 3 is entered via `iretq`
  (CS=0x23/SS=0x1B); syscalls use **`int 0x80`** (write/exit/read/sleep/getpid/
  yield). Each user task rides a kernel thread; the scheduler updates TSS RSP0
  and switches CR3 on every context switch. Ring-3 faults are **isolated**
  (the task is terminated, the kernel survives). A first user program (`init`)
  and a `syscall_test` run from the initramfs. Boots to `MyOS Kernel 0.0.4`.

### What is tested how
- **Tested by normal boot** (`make verify-boot`): bootloader, serial, logging,
  GDT/TSS/IDT, PMM, VMM/CR3, heap, early self-tests.
- **Tested by on-boot scheduler self-tests** (`make verify-scheduler` /
  `verify-timer` / `verify-interrupts` / `verify-preemption`): LAPIC timer
  ticks, context switching, round-robin fairness, sleep/wakeup, and
  quantum-expiry preemption (a deliberately busy thread is preempted while a
  yielding thread keeps making progress).
- **Tested by destructive verification targets:** CPU exceptions
  (`make verify-exception`, #UD) and page faults (`make verify-pagefault`, #PF).
- **Tested across RAM sizes:** `make verify-qemu-matrix` (128M–2048M).
- **Tested by the user/kernel boundary targets:** `make verify-user-build`
  (HXE1 + HXF1 artifacts exist), `verify-initramfs` (loaded + parsed),
  `verify-user-mode` (ring 3 reached, first program exits cleanly),
  `verify-syscalls` (every syscall marker + boundary tests pass),
  `verify-user-fault` (a deliberate ring-3 page fault is isolated and the kernel
  keeps running). `make verify-prompt4` runs the whole chain.
- **Not yet implemented:** full process model (fork/exec/wait), FD model, devfs
  console, keyboard, TTY, shell, VFS, persistent FS, drivers, network, GUI, SMP,
  signals, permissions.
- **Intentionally deferred:** `kfree` reclamation (bump allocator no-op),
  dead-thread stack reclamation, BootServices memory reclaim, I/O APIC
  programming (the PIT line is unrouted; the LAPIC timer is the tick source),
  W^X for user pages (NXE not enabled in Prompt 4).

See [docs/checkpoints.md](docs/checkpoints.md) for the full per-stage table,
PMM/VMM design, and verification status.

## Why the repository is still small
This is an early boot + kernel-foundation project, by design:
- It contains a UEFI bootloader, a kernel, and a tiny ring-3 userland only —
  **no** drivers, shell, GUI, persistent filesystem, or network stack yet.
- The kernel's large `.bss` (stacks + 2 MiB heap arena) is `NOBITS`: it costs
  zero bytes in the ELF file and is only reserved in RAM at load time, so the
  on-disk kernel stays tiny.
- Every claimed capability is backed by a `make verify-*` target rather than
  prose. Small and verified beats large and fake.

Next milestone: **Prompt 5 — process model and interactive userland** (real
process table, spawn/exec/wait, file-descriptor model, devfs console, keyboard
input, TTY layer, init process, shell, and first core utilities).
