# Prompt 5 — Storage and Device Expansion Mega-Phase

MyOS Kernel **0.0.5**. Builds the first serious hardware/storage/input layer:
a driver core, PCI enumeration, a block layer with a write-through cache, MBR/GPT
partitioning, an **AHCI** SATA driver (real read/write under QEMU), an **NVMe**
foundation (discovery + register inspection; block I/O deferred), a custom
persistent filesystem **HNXFS1** mounted at `/disk`, an expanded VFS
(create/mkdir/unlink/stat/mount introspection), a **PS/2 keyboard** + canonical
**TTY** input, an interactive shell mode, and many new coreutils + tests.

## Subsystem map

| Layer | Files |
|-------|-------|
| Driver core | `kernel/driver/{driver,device_id,driver_registry}.{c,h}` |
| PCI | `kernel/pci/{pci,pci_config,pci_device,pci_ids,pci_driver}.{c,h}` |
| Block | `kernel/block/{block_device,block_request,block_cache,block_registry}.{c,h}` |
| Partition | `kernel/partition/{mbr,gpt,partition}.{c,h}` |
| AHCI | `kernel/storage/ahci/{ahci,ahci_controller,ahci_port,ahci_command,ahci_disk}.{c,h}` |
| NVMe | `kernel/storage/nvme/{nvme,nvme_controller,nvme_queue,nvme_namespace,nvme_block}.{c,h}` |
| HNXFS | `kernel/fs/hnxfs/{hnxfs,hnxfs_format,hnxfs_inode,hnxfs_dir,hnxfs_alloc,hnxfs_file}.*` |
| Input | `kernel/input/{input_event,input_queue}.* + ps2/* + keyboard/*` |
| I/O APIC | `kernel/arch/x86_64/ioapic.{c,h}` |
| Tools | `tools/disk/{mkdisk,inspect_disk}.py`, `tools/fs/{mkhnxfs,inspect_hnxfs}.py`, `tools/pci/pci_ids_min.py` |

See the per-subsystem docs: [pci.md](pci.md), [block.md](block.md),
[storage.md](storage.md), [hnxfs.md](hnxfs.md), [input.md](input.md),
[tty.md](tty.md).

## CR3 discipline (carried from Prompt 4)

A user syscall runs under the calling process's CR3, which only mirrors
`[0, 4 MiB)` + framebuffer + LAPIC + initramfs. Operations that touch high
physical RAM or device MMIO from a user-triggered path must run under the kernel
CR3:

* `user_copy_*` validate + copy through the process page tables (kernel CR3).
* **AHCI** transfers (`ahci_disk.c`) bracket their ABAR MMIO + DMA bounce-buffer
  access with `user_with_kernel_cr3()` / `user_restore_cr3()` — poll-only, so
  holding the kernel CR3 across the (non-blocking) window is safe. Each user
  `read`/`write` of a `/disk` file therefore reaches the controller correctly.

## Verification

| Target | Markers |
|--------|---------|
| `verify-pci` | PCI bus scanned, pci enumeration |
| `verify-block` | Block layer online, block cache, partition parser |
| `verify-storage` | AHCI block device online, disk read, disk write |
| `verify-hnxfs` | HNXFS mounted, create/write/read/mkdir/unlink |
| `verify-keyboard` | PS/2 controller online, Keyboard input online, keyboard scripted injection |
| `verify-tty` | TTY interactive input online, tty canonical input, shell interactive smoke |
| `verify-expanded-userland` | expanded coreutils, storage user programs |
| `verify-prompt5` | verify-prompt4 + all of the above + verify-qemu-matrix |

## Status

* **AHCI** — works: real READ/WRITE DMA EXT round-trips under QEMU `ich9-ahci`.
* **NVMe** — discovered; CAP/VS/CSTS read + admin-queue foundation; block I/O
  **deferred** (`[WARN] NVMe block I/O deferred`), no faked success.
* **HNXFS** — persists: writes go through the write-through cache to the AHCI
  disk (and the backing `storage.img`).
* **Keyboard/TTY** — real IRQ1 path (8042 + I/O APIC routing) plus scripted
  injection for headless verification; canonical line discipline (echo,
  backspace, Enter).
* **Shell** — scripted + interactive modes.

## Next milestone

Prompt 6 — USB and hardware compatibility mega-phase: xHCI controller, USB
device enumeration, USB HID keyboard/mouse, improved input stack, PCI MSI/MSI-X
foundation, driver power/reset handling, broader hardware compatibility, and
expanded interactive userland.
