# Storage Drivers

## AHCI (SATA)

PCI driver matching class 0x01 / subclass 0x06 / prog_if 0x01. `ahci_probe`
enables the function, reads **ABAR** (BAR5), and calls `ahci_controller_init`:

1. Map the ABAR 2 MiB page MMIO (cache-disable/write-through via the VMM).
2. Set `GHC.AE` (AHCI enable). (The full HBA reset is intentionally skipped —
   QEMU `ich9-ahci` presents ports ready with AE set; HR perturbs the PHY link.)
3. For each implemented port (`PI`) with a SATA disk (`SSTS` DET=3/IPM=1,
   `SIG`=0x101): rebase, IDENTIFY, register a block device.

Per port (`ahci_port.c`): three DMA pages — command list (1 KiB) + received FIS
(256 B) in one page, one command table, one bounce buffer (one page = 8 sectors).
`ahci_command.c` builds command header slot 0 + a single PRDT entry + an H2D
register FIS, issues `PxCI` bit 0, and polls for completion (`READ/WRITE DMA EXT`
= 0x25/0x35, `IDENTIFY` = 0xEC). The sector count comes from IDENTIFY words
100–103 (LBA48) or 60–61 (LBA28).

`ahci_disk.c` exposes the port as a block device; transfers run with the kernel
CR3 active (see prompt5.md) and chunk into ≤8-sector DMA bursts through the
bounce buffer.

Markers: `[OK] AHCI controller found`, `[OK] AHCI block device online`,
`[PASS] disk read`, `[PASS] disk write`. **AHCI read/write works under QEMU.**

## NVMe (foundation)

PCI driver matching class 0x01 / subclass 0x08. `nvme_controller_init` maps BAR0,
reads **CAP / VS / CSTS**, logs the controller info, and allocates the admin
queue rings (`nvme_queue_foundation`). Namespace identify + I/O queues + block
registration are **deferred**: `[WARN] NVMe block I/O deferred`. No faked
success — no NVMe block device is registered.

## Disk images

`make storage-image` builds `build/image/storage.img` (an MBR disk: p1 = HNXFS,
p2 = scratch) and `build/image/nvme.img`. QEMU attaches them via
`-device ich9-ahci` + `ide-hd` and `-device nvme`. Tools:
`tools/disk/mkdisk.py`, `tools/disk/inspect_disk.py`.
