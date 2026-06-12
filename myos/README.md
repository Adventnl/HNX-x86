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
├── kernel/          ELF64 kernel (entry.S, linker.ld, framebuffer console)
├── tools/           build_image.py / find_ovmf.py / run_qemu.py
└── build/           generated artifacts
```

## Quick start
```bash
make all      # build BOOTX64.EFI + kernel.elf
make image    # build build/image/myos.img (FAT)
make run      # boot in QEMU + OVMF
```

See [docs/build.md](docs/build.md) for host dependencies and
[docs/boot_process.md](docs/boot_process.md) for the full boot flow.

## Disk image
FAT, containing:
```
EFI/BOOT/BOOTX64.EFI
boot/kernel.elf
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
- **Not yet implemented:** user mode, syscalls, processes, I/O APIC routing,
  drivers, FS, network, GUI, SMP, signals, permissions.
- **Intentionally deferred:** `kfree` reclamation (bump allocator no-op),
  dead-thread stack reclamation, BootServices memory reclaim, I/O APIC
  programming (the PIT line is unrouted; the LAPIC timer is the tick source).

See [docs/checkpoints.md](docs/checkpoints.md) for the full per-stage table,
PMM/VMM design, and verification status.

## Why the repository is still small
This is an early boot + kernel-foundation project, by design:
- It contains a UEFI bootloader and a minimal kernel only — **no** drivers,
  userland, GUI, filesystem, or network stack yet.
- The kernel's large `.bss` (stacks + 2 MiB heap arena) is `NOBITS`: it costs
  zero bytes in the ELF file and is only reserved in RAM at load time, so the
  on-disk kernel stays tiny.
- Every claimed capability is backed by a `make verify-*` target rather than
  prose. Small and verified beats large and fake.

Next milestone: **Prompt 4 — user/kernel boundary** (ring 3 transition,
syscall entry, syscall table, user address spaces, simple executable format,
initramfs, first user program, write/exit/read/sleep/getpid/yield syscalls).
