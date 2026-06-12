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

### What is tested how
- **Tested by normal boot** (`make verify-boot`): bootloader, serial, logging,
  GDT/TSS/IDT, PMM, VMM/CR3, heap, early self-tests.
- **Tested by destructive verification targets:** CPU exceptions
  (`make verify-exception`, #UD) and page faults (`make verify-pagefault`, #PF).
- **Tested across RAM sizes:** `make verify-qemu-matrix` (128M–2048M).
- **Not yet implemented:** interrupts/IRQs, timer, scheduler, threads, user
  mode, syscalls, drivers, FS, network, GUI, SMP.
- **Intentionally deferred:** `kfree` reclamation (bump allocator no-op),
  direct-map population, BootServices memory reclaim.

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

Next milestone: **Prompt 3 — interrupt controller and scheduler foundation**
(PIC disable hardening, Local APIC, timer source, IRQ routing, kernel threads,
context switching, round-robin scheduler, sleep/wakeup, timer preemption).
