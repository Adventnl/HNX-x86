# AHCI / SATA Driver (Deep Dive)

This document covers the HNX/MyOS AHCI (Serial ATA) driver: how it is discovered
over PCI, how the HBA is brought up, how a port's DMA structures (command list,
received-FIS area, command table, PRDT) are laid out, how IDENTIFY and READ/WRITE
DMA EXT commands are issued and polled, and how a SATA disk becomes a
`struct block_device` named `diskN`. It is grounded entirely in
`kernel/storage/ahci/`.

The driver is **polled** (no interrupts), uses **command slot 0 only**, and
transfers through a single per-port **bounce buffer** of one page (8 sectors).
It is intentionally compact but real: it talks to actual HBA registers and DMA
structures, issues real ATA commands, and registers a working block device that
HNXFS mounts from.

## Architecture

```
   PCI match: class 0x01 / subclass 0x06 / prog_if 0x01     kernel/storage/ahci/ahci.c
                          |
              ahci_probe: pci_device_enable, ABAR = BAR5
                          |
        ahci_controller_init(abar)                          kernel/storage/ahci/ahci_controller.c
          map ABAR (2MiB MMIO), set GHC.AE, read PI
                          |
          for each implemented port with a SATA disk:
                          |
        ahci_port_has_disk / ahci_port_init                 kernel/storage/ahci/ahci_port.c
          alloc 3 DMA pages, rebase, start, IDENTIFY
                          |
        ahci_disk_register -> struct block_device "diskN"   kernel/storage/ahci/ahci_disk.c
                          |
        block_read/write -> disk_read/write
                          |
        ahci_command_rw (READ/WRITE DMA EXT, polled slot 0) kernel/storage/ahci/ahci_command.c
```

The end-to-end flow from the header comment in `ahci.h`:

> PCI probe (class 1, subclass 6, prog_if 1) → map ABAR → reset HBA → for each
> implemented port with a SATA disk: rebase (command list + FIS + one command
> table in DMA memory), IDENTIFY for the sector count, then register a block
> device whose read/write issue READ/WRITE DMA EXT.

## File map

| File | Role |
| --- | --- |
| `kernel/storage/ahci/ahci.h` | HBA register structs (`hba_mem`, `hba_port`), DMA structs (cmd header / PRDT / cmd table / H2D FIS), `struct ahci_port`, all register/command constants. |
| `kernel/storage/ahci/ahci.c` | PCI driver registration (`g_ahci_driver`) and `ahci_probe`. |
| `kernel/storage/ahci/ahci_controller.h` / `.c` | `ahci_controller_init`: map ABAR, enable AHCI, scan ports, register disks. |
| `kernel/storage/ahci/ahci_port.h` / `.c` | `ahci_port_has_disk`, `ahci_port_init` (stop/rebase/start/IDENTIFY), `stop_port`/`start_port`. |
| `kernel/storage/ahci/ahci_command.h` / `.c` | `ahci_command_rw`, `ahci_command_identify`, the shared `issue()` + `wait_not_busy`. |
| `kernel/storage/ahci/ahci_disk.h` / `.c` | `ahci_disk_register`, `disk_read`/`disk_write`/`disk_rw` (CR3-safe bounce-buffer copy). |
| `kernel/tests/storage_tests.c` | `storage_tests_run`: disk read/write + cache + partition checks. |

## Controller bring-up

`ahci_probe` (`ahci.c`):

```c
static int ahci_probe(const struct pci_device *dev) {
    pci_device_enable(dev);                       /* IO|MEM|MASTER */
    int is_mmio = 0;
    uint64_t abar = pci_device_bar(dev, 5, &is_mmio);   /* ABAR is BAR5 */
    if (!abar || !is_mmio) return -1;
    ahci_controller_init(abar);
    return 0;                                      /* claim the controller */
}
```

`ahci_controller_init` (`ahci_controller.c`):

1. Map the ABAR as a 2 MiB MMIO page:
   `vmm_map_mmio_2m(abar & ~LARGE_PAGE_MASK)`. On failure, return 0 (no disks).
2. Cast `abar` to `struct hba_mem *`.
3. Enable AHCI mode: `hba->ghc |= AHCI_GHC_AE` (bit 31). The full disruptive HBA
   reset (`AHCI_GHC_HR`) is deliberately **skipped** — under QEMU the ports come
   up ready once AE is set.
4. Read `pi = hba->pi` (ports-implemented bitmap).
5. For each bit `i` set in `pi`:
   - `ahci_port_has_disk(&hba->ports[i])` — skip if no SATA disk.
   - `ahci_port_init(&hba->ports[i], i, &port)` — rebase + IDENTIFY; skip on
     failure.
   - `ahci_disk_register(&port)` — on success log markers and count the disk.
6. Return the number of disks registered.

## HBA register layout (`ahci.h`)

```c
struct hba_port {
    volatile uint32_t clb, clbu, fb, fbu;       /* command list base, FIS base */
    volatile uint32_t is, ie, cmd, rsv0;
    volatile uint32_t tfd, sig, ssts, sctl;     /* task file, signature, SATA status/control */
    volatile uint32_t serr, sact, ci, sntf;     /* SATA error, active, command issue */
    volatile uint32_t fbs;
    volatile uint32_t rsv1[11];
    volatile uint32_t vendor[4];
};

struct hba_mem {
    volatile uint32_t cap, ghc, is, pi, vs;     /* capabilities, global host control, ports impl */
    volatile uint32_t ccc_ctl, ccc_pts, em_loc, em_ctl, cap2, bohc;
    volatile uint8_t  rsv[0xA0 - 0x2C];
    volatile uint8_t  vendor[0x100 - 0xA0];
    struct hba_port   ports[32];                /* port registers start at 0x100 */
};
```

Bit / constant definitions:

```c
#define AHCI_GHC_AE   (1u << 31)   /* AHCI enable */
#define AHCI_GHC_HR   (1u << 0)    /* HBA reset   */
#define HBA_PxCMD_ST  (1u << 0)    /* start */
#define HBA_PxCMD_FRE (1u << 4)    /* FIS receive enable */
#define HBA_PxCMD_FR  (1u << 14)   /* FIS receive running */
#define HBA_PxCMD_CR  (1u << 15)   /* command list running */
#define HBA_PxTFD_BSY (1u << 7)
#define HBA_PxTFD_DRQ (1u << 3)
#define HBA_PxTFD_ERR (1u << 0)
#define HBA_PxIS_TFES (1u << 30)   /* task file error status */
#define AHCI_SIG_SATA 0x00000101u

#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_IDENTIFY      0xEC
```

## DMA command structures (`ahci.h`)

```c
struct hba_cmd_header {              /* one per command slot; 32 bytes */
    uint8_t  dw0_lo;                 /* CFL[4:0], A[5], W[6], P[7] */
    uint8_t  dw0_hi;                 /* R, B, C, rsv, PMP[4] */
    uint16_t prdtl;                  /* PRDT length (entries) */
    volatile uint32_t prdbc;         /* bytes transferred */
    uint32_t ctba, ctbau;            /* command table base (phys) */
    uint32_t rsv[4];
} __attribute__((packed));

struct hba_prdt_entry {              /* one scatter/gather entry */
    uint32_t dba, dbau, rsv;         /* data base (phys) */
    uint32_t dbc_i;                  /* [21:0] byte count - 1, [31] interrupt */
} __attribute__((packed));

struct hba_cmd_table {
    uint8_t cfis[64];                /* command FIS */
    uint8_t acmd[16];                /* ATAPI command */
    uint8_t rsv[48];
    struct hba_prdt_entry prdt[8];   /* up to 8 PRDT entries (driver uses 1) */
} __attribute__((packed));

struct fis_reg_h2d {                 /* Register Host-to-Device FIS, type 0x27 */
    uint8_t fis_type;                /* 0x27 */
    uint8_t pmport_c;                /* [7] = command */
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0, lba1, lba2, device;
    uint8_t lba3, lba4, lba5, featureh;
    uint8_t countl, counth, icc, control;
    uint8_t rsv[4];
} __attribute__((packed));
```

### Per-port software state

```c
struct ahci_port {
    struct hba_port *regs;
    uint64_t cmdlist_phys;   /* command list (1 KiB) + received FIS (256 B) in one page */
    uint64_t cmdtable_phys;  /* one command table */
    uint64_t buffer_phys;    /* DMA bounce buffer (1 page = 8 sectors) */
    uint64_t sector_count;
    int      index;
};
```

## Port bring-up (`ahci_port.c`)

### Disk detection

```c
int ahci_port_has_disk(struct hba_port *p) {
    uint32_t ssts = p->ssts;
    uint8_t det = ssts & 0x0F;          /* device detection */
    uint8_t ipm = (ssts >> 8) & 0x0F;   /* interface power mgmt */
    if (det != 3 || ipm != 1) return 0; /* 3 = present+PHY, 1 = active */
    return p->sig == AHCI_SIG_SATA;     /* 0x00000101 = SATA disk */
}
```

### Rebase + IDENTIFY (`ahci_port_init`)

Allocates **three** physical pages (`pmm_alloc_page`):

1. `page_cl` — command list (1 KiB at +0) **plus** the received-FIS area (256 B at
   +1024) in the same page.
2. `page_ct` — one command table.
3. `page_buf` — the DMA bounce buffer (one page = 8 × 512-byte sectors).

If any allocation returns `PMM_INVALID_PAGE`, return -1. The command-list and
command-table pages are zeroed.

Then:

1. `stop_port(regs)` — clear `ST` and `FRE`, spin until `FR` and `CR` clear (or
   `SPIN_LIMIT` = 10,000,000).
2. Program the bases:
   - `regs->clb / clbu` = `page_cl` (low/high 32 bits).
   - `regs->fb / fbu` = `page_cl + 1024` (FIS area).
   - Command header 0's `ctba/ctbau` = `page_ct` (single command table).
3. Clear errors: `regs->serr = 0xFFFFFFFF`, `regs->is = 0xFFFFFFFF`.
4. `start_port(regs)` — spin until `CR` clears, then set `FRE` and `ST`.
5. Run `ahci_command_identify(out)`. On success, read the IDENTIFY buffer (16-bit
   words at `page_buf`): the LBA48 sector count is words 100–103, the LBA28 count
   is words 60–61. `sector_count = lba48 ? lba48 : lba28`.

## Command issue (`ahci_command.c`)

All commands go through one shared `issue()` on **slot 0**, polled:

```c
static int issue(struct ahci_port *port, uint8_t command, uint64_t lba,
                 uint32_t count, uint32_t bytes, int write, int use_lba) {
    struct hba_port *p = port->regs;
    if (wait_not_busy(p) != 0) return -1;          /* BSY|DRQ must clear */
    p->is = (uint32_t)-1;                           /* clear pending IS */

    struct hba_cmd_header *hdr = (..)port->cmdlist_phys;
    hdr->dw0_lo = (sizeof(struct fis_reg_h2d) / 4) & 0x1F;   /* CFL in dwords */
    if (write) hdr->dw0_lo |= (1u << 6);            /* W bit */
    else       hdr->dw0_lo &= ~(1u << 6);
    hdr->dw0_hi = 0;
    hdr->prdtl  = 1;                                 /* one PRDT entry */
    hdr->prdbc  = 0;

    struct hba_cmd_table *tbl = (..)port->cmdtable_phys;
    memset(tbl, 0, sizeof(*tbl));
    tbl->prdt[0].dba   = lo32(port->buffer_phys);
    tbl->prdt[0].dbau  = hi32(port->buffer_phys);
    tbl->prdt[0].dbc_i = (bytes - 1) & 0x3FFFFF;     /* byte count - 1 */

    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = 0x27;
    fis->pmport_c = 0x80;                            /* command bit */
    fis->command  = command;
    if (use_lba) {
        fis->lba0..lba5 = lba bytes;
        fis->device = 0x40;                          /* LBA mode */
        fis->countl/counth = count;
    } else {
        fis->device = 0;
    }

    p->ci = 1;                                        /* issue slot 0 */
    while ((p->ci & 1) && spin < SPIN_LIMIT) {
        if (p->is & HBA_PxIS_TFES) return -1;        /* task file error */
        spin++;
    }
    if (spin >= SPIN_LIMIT) return -1;               /* timeout */
    if (p->tfd & HBA_PxTFD_ERR) return -1;           /* error in task file */
    return 0;
}
```

Public commands:

```c
int ahci_command_rw(struct ahci_port *port, uint64_t lba, uint32_t count, int write);
int ahci_command_identify(struct ahci_port *port);
```

- `ahci_command_rw` rejects `count == 0 || count > 8` (the bounce buffer is one
  page = 8 sectors). It picks `WRITE_DMA_EXT (0x35)` or `READ_DMA_EXT (0x25)` and
  calls `issue(..., bytes = count*512, write, use_lba=1)`.
- `ahci_command_identify` issues `IDENTIFY (0xEC)` with `bytes = 512`,
  `use_lba = 0`.

## Disk → block device (`ahci_disk.c`)

`ahci_disk_register`:
- `kcalloc` a `block_device` and `kmalloc` a private `ahci_port` copy (`*saved =
  *port`), so the block device owns a stable copy of the port state.
- Name: `"disk"` + a single digit `('0' + g_disk_index++)` → `disk0`, `disk1`, ...
- `sector_count = saved->sector_count`, `sector_size = 512`,
  `driver_data = saved`, `read = disk_read`, `write = disk_write`.
- `block_register_device(dev)`.

`disk_rw` is the transfer engine and is **CR3-aware**:

```c
static int disk_rw(struct block_device *dev, uint64_t lba, void *buffer,
                   uint32_t count, int write) {
    struct ahci_port *port = dev->driver_data;
    uint8_t *p = buffer;
    uint64_t saved = user_with_kernel_cr3();         /* ensure ABAR + DMA reachable */
    int rc = 0;
    while (count > 0) {
        uint32_t chunk = (count > 8) ? 8 : count;    /* clamp to bounce buffer */
        if (write) {
            memcpy(port->buffer_phys, p, chunk * 512);
            if (ahci_command_rw(port, lba, chunk, 1) != 0) { rc = -1; break; }
        } else {
            if (ahci_command_rw(port, lba, chunk, 0) != 0) { rc = -1; break; }
            memcpy(p, port->buffer_phys, chunk * 512);
        }
        p += chunk * 512; lba += chunk; count -= chunk;
    }
    user_restore_cr3(saved);
    return rc;
}
```

The CR3 switch matters: a block transfer may originate from a user syscall under
a *user* CR3, but the ABAR MMIO and the DMA bounce buffer live in the kernel
address space. `user_with_kernel_cr3()` swaps to the kernel CR3 for the duration
and `user_restore_cr3()` swaps back. The window is **poll-only** (no sleep), so
nothing else runs that would need the user CR3 mid-transfer.

## Data structures (summary)

| Struct | File | Purpose |
| --- | --- | --- |
| `struct hba_mem` / `struct hba_port` | `ahci.h` | Memory-mapped HBA + port registers (volatile). |
| `struct hba_cmd_header` | `ahci.h` | Command-list slot descriptor. |
| `struct hba_prdt_entry` | `ahci.h` | One scatter/gather entry; driver uses 1. |
| `struct hba_cmd_table` | `ahci.h` | CFIS + ACMD + 8 PRDT entries. |
| `struct fis_reg_h2d` | `ahci.h` | Register H2D FIS (type 0x27). |
| `struct ahci_port` | `ahci.h` | Per-port software state (phys addrs, sector count). |
| `struct block_device` | `block_device.h` | Registered as `diskN`; `driver_data` = saved `ahci_port`. |

## Key APIs

```c
void ahci_init(void);                                     /* register g_ahci_driver with PCI */
int  ahci_controller_init(uint64_t abar);                 /* returns disk count */
int  ahci_port_has_disk(struct hba_port *p);
int  ahci_port_init(struct hba_port *regs, int index, struct ahci_port *out);
int  ahci_command_rw(struct ahci_port *port, uint64_t lba, uint32_t count, int write);
int  ahci_command_identify(struct ahci_port *port);
int  ahci_disk_register(struct ahci_port *port);
```

## Invariants

- **ABAR is BAR5, memory-mapped.** `ahci_probe` requires `pci_device_bar(dev, 5,
  &is_mmio)` non-zero and `is_mmio`.
- **AHCI mode is enabled without a full HBA reset.** Only `GHC.AE` is set; the
  disruptive `GHC.HR` reset is skipped (QEMU presents ports ready).
- **One command slot, one PRDT entry.** Every transfer uses slot 0 (`p->ci = 1`)
  and `prdtl = 1`. The command header's `ctba` points at the single command
  table set up at rebase.
- **Transfers are capped at 8 sectors per command.** The bounce buffer is one
  page; `ahci_command_rw` rejects `count > 8`; `disk_rw` chunks larger requests.
- **All I/O goes through the bounce buffer.** `disk_rw` memcpys to/from
  `port->buffer_phys`; the caller's buffer is never DMA'd directly.
- **The port must be idle before issue.** `issue` calls `wait_not_busy` (BSY|DRQ
  clear) and clears `IS` before setting `CI`.
- **Errors are checked two ways.** Mid-poll on `IS.TFES` (bit 30), and post-poll
  on `TFD.ERR` (bit 0). Either aborts with -1.
- **A disk owns a private copy of its port state.** `ahci_disk_register` saves
  `*port` so the block device does not alias the controller-init stack frame.
- **Sector size is 512.** Hard-coded in `disk_rw` (`chunk * 512`) and
  `ahci_disk_register` (`sector_size = 512`).
- **Kernel CR3 is active during a transfer.** `disk_rw` brackets the whole loop
  with `user_with_kernel_cr3()` / `user_restore_cr3()`.

## Failure modes

- **ABAR absent / not MMIO →** `ahci_probe` returns -1; the controller is not
  claimed.
- **ABAR MMIO map fails →** `ahci_controller_init` returns 0 (no disks).
- **No SATA disk on a port →** `ahci_port_has_disk` returns 0; the port is
  skipped.
- **DMA page allocation fails →** `ahci_port_init` returns -1; the port is
  skipped.
- **IDENTIFY fails →** `ahci_port_init` still returns 0 but leaves
  `sector_count = 0`; the disk registers with zero sectors.
- **Command timeout →** the `SPIN_LIMIT` (10,000,000) poll loop expires and
  `issue` returns -1; `disk_rw` aborts that transfer with -1.
- **Task file error →** `IS.TFES` or `TFD.ERR` makes `issue` return -1.
- **`count == 0` or `> 8` to `ahci_command_rw` →** rejected with -1 (but `disk_rw`
  never passes > 8 because it chunks).
- **`kcalloc`/`kmalloc` failure in register →** `ahci_disk_register` returns -1;
  no block device.

## Verification

```
make verify-storage   # AHCI disk online + read/write round-trip
make verify-block     # partition parser sees disk0p1 (built on AHCI)
make verify-prompt5   # full chain incl. the above
```

Serial markers and emitters:

| Marker | Emitter |
| --- | --- |
| `[OK] AHCI controller found` | `ahci_probe` (`ahci.c`) |
| `[OK] AHCI block device online` | `ahci_controller_init` (per registered disk) |
| `[PASS] disk read` | `storage_tests_run`: LBA 0 returns `0x55 0xAA` MBR signature |
| `[PASS] disk write` | `storage_tests_run`: raw write+read round-trip on `disk0p2` |
| `[PASS] block cache` | `storage_tests_run`: re-read of LBA 0 hits the cache |
| `[PASS] partition parser` | `storage_tests_run`: `disk0p1` registered |

`verify-storage` expects `[OK] AHCI block device online`, `[PASS] disk read`,
`[PASS] disk write`. The read test proves a *real* sector came back (the MBR
signature), and the write test deliberately uses the raw `disk0p2->write`/`read`
ops (bypassing the cache) so the data really reaches the platter and back.

The image build attaches the storage disk so QEMU presents an AHCI controller and
a partitioned disk; `disk0p1` carries the HNXFS that mounts at `/disk`, and
`disk0p2` is the scratch partition the write test round-trips through. The
expanded suite (`Makefile.production`) adds `verify-ahci-expanded` to
`verify-production-200k`.

## Future expansion

- **Interrupt-driven completion.** Today every command is polled to `SPIN_LIMIT`.
  Wiring the port `IE`/`IS` registers and an MSI/MSI-X vector (the PCI layer
  already programs MSI; see `docs/pci_deep.md`) would let transfers complete
  asynchronously and integrate with the block request queue.
- **Multiple command slots / NCQ.** The HBA supports 32 slots and Native Command
  Queuing; the driver uses slot 0 only. Multi-slot issue would allow concurrent
  in-flight commands.
- **Multi-entry PRDT / direct DMA.** `prdt[8]` exists but only one entry is used,
  forcing the bounce-buffer copy and the 8-sector cap. Scatter/gather PRDTs over
  the caller's pages would remove both.
- **Larger transfers.** Removing the one-page bounce buffer (or making it larger)
  lifts the 8-sector-per-command limit.
- **Full HBA reset + port COMRESET.** The disruptive `GHC.HR` reset and SATA
  `SCTL` link reset are skipped (fine for QEMU); real hardware bring-up and error
  recovery would need them.
- **Write-back integration.** Once the block cache gains a write-back mode (see
  `docs/block_layer.md`), AHCI writes would be flushed in batches and a `FLUSH
  CACHE EXT` command added for durability.
