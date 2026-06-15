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
│   ├── user/        ring-3 entry, syscalls, address spaces, HXE1 loader, copy
│   ├── fs/          VFS core + ramfs/ + devfs/ + hnxfs/ (persistent FS)
│   ├── device/      device registry + char devices (null/zero)
│   ├── tty/         /dev/console + TTY (canonical input)
│   ├── process/     process model, process table, fd table, exec, wait
│   ├── driver/      driver core (driver/device/registry)
│   ├── pci/         PCI config + enumeration + driver matching
│   ├── block/       block device + request + write-through cache + registry
│   ├── partition/   MBR + GPT parsers
│   ├── storage/     ahci/ (SATA) + nvme/ (foundation)
│   └── input/       input event/queue + ps2/ + keyboard/
├── user/            ring-3 userland (no host libc)
│   ├── include/lib/ headers + runtime (crt0/syscall/unistd/stdio/string/stdlib/malloc)
│   ├── init/ shell/ coreutils/ tests/ storage/ etc/
├── tools/           build_image / find_ovmf / run_qemu / mkhxe / mkinitramfs
│   ├── disk/        mkdisk / inspect_disk
│   ├── fs/          mkhnxfs / inspect_hnxfs
│   └── pci/         pci_ids_min
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
- **Prompt 4 (userland foundation mega-phase):** a full first user/kernel +
  userland layer. The bootloader loads a custom **HXF1 initramfs**; the kernel
  exposes it as a read-only **ramfs** mounted at `/`, with a **devfs** at `/dev`
  (`console`/`null`/`zero`) over a small **VFS** (`vnode`/`file`/mount table,
  path resolution, open/read/write/lseek/readdir/stat). A real **process model**
  (PID, parent, state, fd table, cwd) loads custom **HXE1** executables into
  per-process **address spaces** and runs them in ring 3 (`iretq`,
  CS=0x23/SS=0x1B). **17 `int 0x80` syscalls** (exit/write/read/sleep/getpid/
  yield/open/close/lseek/readdir/spawn/wait/getcwd/chdir/uptime/meminfo/ps)
  dispatch through a static table with software-validated user pointers.
  `/bin/init.hxe` (PID 1) runs five user tests, then a **scripted shell** that
  spawns 15 **coreutils**. Ring-3 faults are **isolated** (the process dies, the
  kernel survives). Boots to `MyOS Kernel 0.0.4`.
- **Prompt 5 (storage + device + input mega-phase):** a **driver core** +
  **PCI** enumeration (CF8/CFC), a **block layer** with a write-through cache,
  **MBR/GPT** partitioning, an **AHCI** SATA driver (real READ/WRITE DMA EXT
  under QEMU), an **NVMe** foundation (discovery + CAP/VS/CSTS; block I/O
  deferred), a custom persistent filesystem **HNXFS1** mounted at `/disk`
  (create/write/read/mkdir/unlink, write-through to disk), an expanded **VFS**
  (mkdir/unlink/create/stat + 6 new syscalls), a **PS/2 keyboard** routed via the
  **I/O APIC** with a canonical **TTY** line discipline, an **interactive shell**
  mode, and ~14 new coreutils (mkdir/rm/touch/writefile/readfile/hexdump/stat/
  mounts/devices/blocks/lspci/lsblk/…). Boots to `MyOS Kernel 0.0.5`.
- **Prompt 6 (USB + hardware compatibility mega-phase):** **PCI capability
  parsing** + an **MSI/MSI-X foundation**, a **driver lifecycle** (discovered→
  matched→active→suspended/failed/removed) with power/reset hooks and a
  **hardware event bus**, a from-scratch **xHCI** USB host controller (MMIO
  bring-up, command/event TRB rings, device/input contexts, control + interrupt
  transfers, root-hub scan), a **USB core** (descriptor parser, enumeration,
  configuration), a **USB HID** boot keyboard + mouse, a **unified input stack**
  merging PS/2 and USB input under one event model (text → TTY, mouse → event
  queue), ~17 new HW/USB/input userland tools + 6 new syscalls, and offline
  decoder tooling. Genuinely enumerates QEMU's `usb-kbd`/`usb-mouse`. Boots to
  `MyOS Kernel 0.0.6`. See [docs/prompt6.md](docs/prompt6.md),
  [docs/xhci.md](docs/xhci.md), [docs/usb.md](docs/usb.md),
  [docs/hid.md](docs/hid.md), [docs/msi.md](docs/msi.md),
  [docs/driver_lifecycle.md](docs/driver_lifecycle.md),
  [docs/hardware_compatibility.md](docs/hardware_compatibility.md).

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
- **Tested by the userland-foundation targets:** `make verify-user-build`
  (all `/bin` + `/tests` HXE and the initramfs exist), `verify-initramfs` (the
  archive contains every required file), `verify-user-mode` (ring 3 reached,
  `[USER] hello from ring 3`), `verify-syscalls` (`[PASS] syscall_test`),
  `verify-vfs` (`[PASS] vfs_test` + `fd_test`), `verify-process`
  (`[PASS] spawn_test`), `verify-shell` (`[PASS] shell scripted session`),
  `verify-user-fault` (a deliberate ring-3 page fault is isolated). `make
  verify-prompt4` runs the whole chain.
- **Tested by the storage/device/input targets:** `verify-pci`, `verify-block`,
  `verify-storage` (AHCI disk read/write), `verify-hnxfs` (persistent FS),
  `verify-keyboard`, `verify-tty` (canonical input + interactive shell),
  `verify-expanded-userland`. `make verify-prompt5` runs the whole chain.
- **Tested by the USB/hardware targets (Prompt 6):** `verify-msi`,
  `verify-driver-lifecycle`, `verify-xhci`, `verify-usb`, `verify-hid`,
  `verify-input-unified`, `verify-hw-userland`. `make verify-prompt6` runs the
  whole chain (including `verify-prompt5` + the memory matrix).
- **Not yet implemented (Prompt 7+):** networking, GUI, audio, SMP, journaling
  FS, full permissions, dynamic linker, package manager. USB mass storage and
  NVMe block I/O are documented foundations (deferred). MSI/MSI-X is a
  parsed/programmable foundation (controllers still poll).
- **Intentionally deferred:** `kfree` reclamation (bump allocator no-op; user
  memory *is* reclaimed on process reap via the PMM), dead-thread stack
  reclamation, BootServices memory reclaim, I/O APIC programming (the LAPIC
  timer is the tick source), W^X / NXE for user pages.

See [docs/checkpoints.md](docs/checkpoints.md) for the full per-stage table,
PMM/VMM design, and verification status.

## Scale philosophy
The codebase grows by real feature depth, not padding. Prompt 4 adds a broad
userland foundation (VFS, devfs, processes, fd tables, a shell, 15 coreutils)
with compact, reusable subsystems and a verification target behind every claim.
The kernel's large `.bss` (stacks + 2 MiB heap arena) is `NOBITS` — zero bytes
on disk, reserved only at load — so the on-disk kernel stays small while the
mapped footprint stays below `USER_IMAGE_BASE` (4 MiB).

Next milestone: **Prompt 7 — networking mega-phase** (PCI NIC drivers, Ethernet,
ARP, IPv4, ICMP ping, UDP, DHCP, DNS, a TCP foundation, a sockets API, network
userland tools, and a network verification matrix).

Previously: **Prompt 6 — USB and hardware compatibility mega-phase** (PCI
capability parsing, MSI/MSI-X foundation, driver lifecycle + hardware event bus,
a from-scratch xHCI controller, USB core + descriptor parser, USB HID
keyboard/mouse, a unified PS/2+USB input stack, and expanded HW/USB/input
userland). Boots to `MyOS Kernel 0.0.6`.

Earlier: **Prompt 5 — storage and device expansion mega-phase** (PCI
device manager, AHCI/NVMe block devices, block cache, a simple persistent
filesystem, expanded VFS, PS/2 keyboard, real TTY input, an interactive shell,
more coreutils, and a driver verification matrix).
