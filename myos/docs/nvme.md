# NVMe Driver

This document covers the HNX/MyOS NVMe driver as it exists in the tree. It is
grounded in `kernel/storage/nvme/`.

**Honesty note up front:** the NVMe driver is a *foundation*, not a complete
block driver. As stated in the source headers, "Block I/O is deferred (Prompt 5
scope) — the controller is identified and its capabilities logged, but no
namespace block device is registered." Concretely, the current code:

- discovers the controller over PCI and maps BAR0;
- reads and logs the controller capability/version/status registers;
- allocates the **admin** submission/completion queue rings (a foundation that is
  *not yet driven* — no commands are submitted);
- probes namespaces only to log the deferral, and registers **no** block device
  (`nvme_block_register_deferred` logs a warning rather than faking success).

This document describes what is there precisely, marks every unimplemented step
clearly, and then lays out the spec-compliant completion path under *Future
expansion*. Nothing below claims behavior the code does not have.

## Architecture

```
   PCI match: class 0x01 / subclass 0x08 (any prog_if)   kernel/storage/nvme/nvme.c
                          |
              nvme_probe: pci_device_enable, BAR0
                          |
        nvme_controller_init(bar0, &ctrl)                 kernel/storage/nvme/nvme_controller.c
          map BAR0 (2MiB MMIO), read CAP/VS/CSTS, log
                          |
            nvme_queue_foundation()                       kernel/storage/nvme/nvme_queue.c
              alloc admin SQ + CQ pages (NOT driven)
                          |
            nvme_namespace_probe(ctrl)                    kernel/storage/nvme/nvme_namespace.c
              -> nvme_block_register_deferred()           kernel/storage/nvme/nvme_block.c
                 logs "[WARN] NVMe block I/O deferred"
                 (registers no block device)
```

Compare with AHCI (`docs/ahci_deep.md`), which completes the chain through
IDENTIFY, READ/WRITE DMA EXT, and `block_register_device`. NVMe stops after
register inspection + admin-queue allocation. The two drivers share the same PCI
matcher and the same `pci_driver_match_all()` boot call.

### Boot wiring (`kernel/src/kernel.c`)

```c
ahci_init();
nvme_init();                  /* pci_driver_register(&g_nvme_driver) */
pci_driver_match_all();       /* probes AHCI (+ NVMe foundation) */
```

`nvme_init` only registers the PCI driver; the actual `nvme_probe` runs inside
`pci_driver_match_all()` when a matching function is found.

## File map

| File | Role | State |
| --- | --- | --- |
| `kernel/storage/nvme/nvme.h` | Controller MMIO register offsets; `nvme_init` prototype. | complete |
| `kernel/storage/nvme/nvme.c` | PCI driver (`g_nvme_driver`) + `nvme_probe`. | complete |
| `kernel/storage/nvme/nvme_controller.h` / `.c` | `struct nvme_controller`; `nvme_controller_init` (register inspection). | register read only |
| `kernel/storage/nvme/nvme_queue.h` / `.c` | `struct nvme_queue`; `nvme_queue_foundation` (allocates admin rings). | rings allocated, not driven |
| `kernel/storage/nvme/nvme_namespace.h` / `.c` | `nvme_namespace_probe` (deferral only). | deferred |
| `kernel/storage/nvme/nvme_block.h` / `.c` | `nvme_block_register_deferred` (logs warning). | deferred |

There is no NVMe self-test file and no `[PASS] nvme ...` marker emitted by code
that runs today (see Verification).

## Controller register offsets (`nvme.h`)

```c
#define NVME_REG_CAP   0x00    /* 64-bit controller capabilities */
#define NVME_REG_VS    0x08    /* version */
#define NVME_REG_CC    0x14    /* controller configuration */
#define NVME_REG_CSTS  0x1C    /* controller status */
#define NVME_REG_AQA   0x24    /* admin queue attributes */
#define NVME_REG_ASQ   0x28    /* admin submission queue base */
#define NVME_REG_ACQ   0x30    /* admin completion queue base */
```

Of these, only `CAP`, `VS`, and `CSTS` are currently **read**. `CC`, `AQA`,
`ASQ`, and `ACQ` are defined but never written — they are the registers a real
enable sequence would program (see Future expansion).

The register window is reached through a tiny accessor in `nvme_controller.c`:

```c
static uint32_t reg32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}
```

All current reads are 32-bit. `CAP` is 64-bit, so it is assembled from two reads
(`reg32(regs, 0x00)` and `reg32(regs, 0x04)`).

### CAP fields the driver decodes

`CAP` (Controller Capabilities, offset 0x00, 64-bit) carries everything the
foundation needs. Only the two fields below are decoded today; the rest are read
into `out->cap` and logged verbatim but not interpreted.

| Field | CAP bits | Source in code | Meaning |
| --- | --- | --- | --- |
| MQES | 15:0 (low dword) | `(cap_lo & 0xFFFF) + 1` → `max_queue_entries` | Maximum Queue Entries Supported, zero-based; the `+1` makes it a real count. |
| DSTRD | 35:32 (= bits 3:0 of the high dword) | `(cap_hi >> 0) & 0xF` → `doorbell_stride` | Doorbell Stride; the inter-doorbell spacing exponent. |

Fields NOT decoded by the current code (left for the enable path):

| Field | CAP bits | Why it matters later |
| --- | --- | --- |
| TO | 31:24 (low dword) | Timeout (in 500 ms units) to wait for `CSTS.RDY` to flip after `CC.EN` is toggled. |
| CSS | 44:37 | Command set(s) supported; drives `CC.CSS`. |
| MPSMIN / MPSMAX | 51:48 / 55:52 | Memory page size bounds; drives `CC.MPS`. |

`VS` (Version, offset 0x08) is the NVMe spec version (major/minor/tertiary packed
into 32 bits); the code logs it raw. `CSTS` (Controller Status, offset 0x1C) holds
`RDY` (bit 0) and `CFS` (Controller Fatal Status, bit 1); the code logs it raw and
does not yet poll `RDY`.

### Queue geometry and doorbells

NVMe places its submission/completion **doorbell** registers at a fixed area
starting at offset 0x1000 in the BAR, spaced by the doorbell stride. The stride
in bytes is `4 << CAP.DSTRD`, and the doorbell for a given queue is:

```
SQyTDBL (submission tail) @ 0x1000 + (2*y    ) * (4 << DSTRD)
CQyHDBL (completion head) @ 0x1000 + (2*y + 1) * (4 << DSTRD)
```

The foundation already captures `doorbell_stride` (CAP.DSTRD) for exactly this
computation, but **no doorbell is ever rung today** — there is no command traffic.
The admin queue (queue 0) would use `SQ0TDBL`/`CQ0HDBL`; I/O queues (1..N) follow.

The admin ring geometry the foundation chose:

| Quantity | Value | Reason |
| --- | --- | --- |
| SQ entry size | 64 bytes | NVMe fixed admin/I/O SQ entry size (IOSQES = 6 → 2^6). |
| CQ entry size | 16 bytes | NVMe fixed CQ entry size (IOCQES = 4 → 2^4). |
| SQ entries per page | `PAGE_SIZE / 64` = 64 | `g_admin.entries`, one 4 KiB page. |
| Phase tag start | 1 | First completion lap has phase bit 1 (`g_admin.phase = 1`). |

The `phase` tag is how a poller distinguishes a fresh completion from a stale one
without a separate valid bit: each time `cq_head` wraps, the expected phase flips.
The field is initialized but unused until completion polling exists.

## Data structures

### `struct nvme_controller` (`nvme_controller.h`)

```c
struct nvme_controller {
    volatile uint8_t *regs;        /* BAR0 MMIO base */
    uint64_t cap;                  /* full 64-bit CAP */
    uint32_t version;              /* VS */
    uint32_t doorbell_stride;      /* CAP.DSTRD */
    uint32_t max_queue_entries;    /* CAP.MQES + 1 */
};
```

Populated by `nvme_controller_init`:
- `cap` = `(cap_hi << 32) | cap_lo` from `CAP`/`CAP+4`.
- `version` = `VS`.
- `max_queue_entries` = `(cap_lo & 0xFFFF) + 1` (CAP.MQES is a zero-based max).
- `doorbell_stride` = `(cap_hi >> 0) & 0xF` (CAP.DSTRD, CAP bits 35:32).

> Note: the controller struct is filled on the stack in `nvme_probe`
> (`struct nvme_controller ctrl;`) and is **not** retained after probe returns —
> there is no persistent controller registry yet, because nothing downstream
> consumes it.

### `struct nvme_queue` (`nvme_queue.h`)

```c
struct nvme_queue {
    uint64_t sq_phys;       /* submission queue ring (phys) */
    uint64_t cq_phys;       /* completion queue ring (phys) */
    uint32_t entries;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint8_t  phase;         /* completion phase tag */
};
```

A single static admin queue `g_admin` (in `nvme_queue.c`) is initialized by
`nvme_queue_foundation`:
- `sq_phys` / `cq_phys` = one `pmm_alloc_page()` each (both zeroed).
- `entries` = `PAGE_SIZE / 64` (64-byte SQ entries → 64 entries per page).
- `sq_tail = 0`, `cq_head = 0`, `phase = 1`.

This memory is allocated but never registered with the controller (ASQ/ACQ are
not written) and no commands are ever placed in it.

## Key APIs

```c
void nvme_init(void);                                          /* register PCI driver */
int  nvme_controller_init(uint64_t bar0, struct nvme_controller *out);
int  nvme_queue_foundation(void);                              /* alloc admin rings */
void nvme_namespace_probe(struct nvme_controller *ctrl);       /* deferral only */
void nvme_block_register_deferred(void);                       /* logs warning */
```

### `nvme_probe` (`nvme.c`)

```c
static int nvme_probe(const struct pci_device *dev) {
    pci_device_enable(dev);                          /* IO|MEM|MASTER */
    int is_mmio = 0;
    uint64_t bar0 = pci_device_bar(dev, 0, &is_mmio);  /* NVMe regs are BAR0 */
    if (!bar0 || !is_mmio) return -1;
    struct nvme_controller ctrl;
    if (nvme_controller_init(bar0, &ctrl) != 0) return -1;
    return 0;
}

static struct pci_driver g_nvme_driver = {
    .name = "nvme", .class_code = 0x01, .subclass = 0x08,
    .prog_if = 0x02, .match_prog_if = 0,             /* match any NVMe prog_if */
    .probe = nvme_probe,
};
```

`match_prog_if = 0` means the matcher pairs on class 0x01 / subclass 0x08 alone,
ignoring prog_if (so any NVMe controller matches regardless of its prog_if value).

### `nvme_controller_init` (`nvme_controller.c`)

1. Map BAR0 as a 2 MiB MMIO page:
   `vmm_map_mmio_2m(bar0 & ~LARGE_PAGE_MASK)`; return -1 on failure.
2. `out->regs = (volatile uint8_t *)bar0`.
3. Read `CAP` (lo+hi), `VS`, `CSTS`.
4. Derive `max_queue_entries` and `doorbell_stride` (see struct above).
5. Log:
   ```
   [OK] NVMe controller found
       nvme cap       : <CAP>
       nvme version   : <VS>
       nvme csts      : <CSTS>
       nvme max qe    : <MQES+1>
   ```
6. `nvme_queue_foundation()` — allocate the admin rings (not driven).
7. `nvme_namespace_probe(out)` — deferral.
8. Return 0.

### `nvme_namespace_probe` / `nvme_block_register_deferred`

```c
void nvme_namespace_probe(struct nvme_controller *ctrl) {
    (void)ctrl;
    /* Identify Namespace + I/O queue creation are later work. */
    nvme_block_register_deferred();
}

void nvme_block_register_deferred(void) {
    kernel_log_warn("NVMe block I/O deferred");      /* "[WARN] NVMe block I/O deferred" */
}
```

No Identify command is issued, no namespace is enumerated, and no
`block_register_device` is called — by design, to avoid faking a working disk.

## Probe call sequence (what actually runs)

When `pci_driver_match_all()` (called from `kernel_main`) finds a class 0x01 /
subclass 0x08 function, the exact call chain that executes today is:

```
pci_driver_match_all                      kernel/pci/pci_driver.c
  -> matches(g_nvme_driver, dev)          class+subclass only (match_prog_if=0)
  -> nvme_probe(dev)                       kernel/storage/nvme/nvme.c
       pci_device_enable(dev)              command reg: IO|MEM|MASTER
       pci_device_bar(dev, 0, &is_mmio)    BAR0 base
       nvme_controller_init(bar0, &ctrl)   kernel/storage/nvme/nvme_controller.c
         vmm_map_mmio_2m(...)              map BAR0 (2 MiB MMIO page)
         reg32(CAP/CAP+4/VS/CSTS)          read + derive cap/version/mqes/dstrd
         kernel_log_ok / kernel_log_hex64  emit the discovery block
         nvme_queue_foundation()           kernel/storage/nvme/nvme_queue.c
           pmm_alloc_page() x2             admin SQ + CQ pages, zeroed (inert)
         nvme_namespace_probe(&ctrl)       kernel/storage/nvme/nvme_namespace.c
           nvme_block_register_deferred()  kernel/storage/nvme/nvme_block.c
             kernel_log_warn(...)          "[WARN] NVMe block I/O deferred"
       return 0                             controller claimed
```

Everything after `nvme_queue_foundation()` is observation/deferral; no controller
register is written, so the NVMe device is left exactly as firmware presented it.

## Comparison with AHCI

NVMe and AHCI share the PCI matcher, the `pci_device_enable` + BAR + 2 MiB MMIO
map idiom, and the same boot call (`pci_driver_match_all`). They diverge at the
point of doing real I/O:

| Step | AHCI (`docs/ahci_deep.md`) | NVMe (this driver) |
| --- | --- | --- |
| PCI match | class 1 / subclass 6 / prog_if 1 (prog_if matched) | class 1 / subclass 8 (prog_if ignored) |
| Register BAR | BAR5 (ABAR) | BAR0 |
| Controller enable | sets `GHC.AE` | none (CC.EN never set) |
| Queues | command list + FIS + cmd table (per port) | admin SQ/CQ pages allocated but **inert** |
| Device identify | `IDENTIFY` (0xEC), reads sector count | none (Identify deferred) |
| Data path | `READ/WRITE DMA EXT`, polled, bounce buffer | none (I/O deferred) |
| Block device | `block_register_device("diskN")` | **none** (deferred, logs warning) |
| Partitions | scanned (`disk0p1`, ...) | nothing to scan |

## Invariants

- **BAR0 is the NVMe register window, memory-mapped.** `nvme_probe` requires
  `pci_device_bar(dev, 0, &is_mmio)` non-zero and `is_mmio`.
- **Matching ignores prog_if.** `match_prog_if = 0`, so any class 0x01 / subclass
  0x08 function is claimed.
- **The controller is never enabled.** `CC.EN` is not set; `CSTS.RDY` is not
  waited on; the controller stays in whatever state firmware left it (the code
  only *reads* `CSTS`).
- **The admin queue is inert.** `g_admin` memory exists but ASQ/ACQ are never
  programmed and no SQ entry is ever written, so no admin command ever runs.
- **No NVMe block device exists.** `block_get_device("nvme0")` (or any NVMe name)
  returns NULL; the partition scanner finds nothing to scan for NVMe.
- **`max_queue_entries` is `MQES + 1`.** CAP.MQES is zero-based; the code adds 1.
- **Admin ring sizing assumes 64-byte SQ entries.** `entries = PAGE_SIZE / 64`.

## Failure modes

- **BAR0 absent / not MMIO →** `nvme_probe` returns -1; controller not claimed.
- **BAR0 MMIO map fails →** `nvme_controller_init` returns -1; `nvme_probe`
  returns -1.
- **Admin ring allocation fails →** `nvme_queue_foundation` returns -1 if either
  `pmm_alloc_page` yields `PMM_INVALID_PAGE`. (`nvme_controller_init` does not
  check this return value, so a foundation allocation failure does not abort the
  probe — it only means the inert rings were not allocated.)
- **Namespace / block I/O →** always "fails closed": no device is registered, and
  a `[WARN] NVMe block I/O deferred` line is logged. This is the intended state,
  not an error path.

## Verification

There is **no** `[OK]`/`[PASS]` NVMe marker emitted by completed block I/O
because there is none. What is observable today on the serial log when an NVMe
controller is present:

| Marker | Emitter | Meaning |
| --- | --- | --- |
| `[OK] NVMe controller found` | `nvme_controller_init` | controller discovered, registers read |
| `[WARN] NVMe block I/O deferred` | `nvme_block_register_deferred` | block I/O intentionally not implemented |

The expanded production suite declares a `verify-nvme` target
(`Makefile.production` `.PHONY` list, and it is invoked by
`verify-production-200k` in the main `Makefile`). It boots the real image under
QEMU like the other `verify-*` targets (`tools/verify_qemu.py --image $(IMAGE)
--expect ...`), but because NVMe block I/O is deferred, the markers it can
legitimately assert against today are the discovery/deferral lines above — there
is no faked pass string (the convention, per `Makefile.production`, is that every
marker is "emitted by code that actually ran").

Whether QEMU presents an NVMe controller depends on the image's device flags; the
AHCI path is the one wired into the booting filesystem (`disk0p1` → HNXFS at
`/disk`). NVMe presence is therefore optional and the driver degrades to the two
log lines above.

To exercise it manually you would attach an NVMe device to QEMU and watch COM1
for `[OK] NVMe controller found`.

## Future expansion

This is the spec-compliant completion path. None of it is implemented yet; the
foundation above is the starting point.

1. **Controller enable/reset sequence.** Clear `CC.EN`, poll `CSTS.RDY == 0`;
   program `AQA` (admin queue sizes), `ASQ`/`ACQ` (the `g_admin.sq_phys` /
   `cq_phys` already allocated by `nvme_queue_foundation`); set `CC` (IOSQES = 6
   for 64-byte SQ entries, IOCQES = 4 for 16-byte CQ entries, MPS, CSS, then
   `CC.EN`); poll `CSTS.RDY == 1`. The register offsets (`CC`, `AQA`, `ASQ`,
   `ACQ`) are already defined in `nvme.h`.
2. **Admin command submission + completion.** Drive `g_admin`: write a 64-byte
   command at `sq_phys[sq_tail]`, ring the SQ tail doorbell (computed from
   `doorbell_stride`), poll the CQ for a completion matching the current `phase`,
   advance `cq_head` and ring the CQ head doorbell.
3. **Identify Controller (CNS 0x01) and Identify Namespace (CNS 0x00).** Read the
   active namespace list, then per-namespace get the size in LBAs and the LBA
   format (LBADS → sector size).
4. **Create I/O queues.** Issue Create I/O Completion Queue and Create I/O
   Submission Queue admin commands, allocating their rings the same way
   `nvme_queue_foundation` allocates the admin rings.
5. **I/O Read (opcode 0x02) / Write (opcode 0x01).** Build PRP lists (or PRP1/PRP2
   for small transfers) over the data buffer; submit on the I/O SQ; poll the I/O
   CQ. Mirror the CR3-safe pattern AHCI uses in `disk_rw` so transfers from a user
   syscall reach the BAR0 MMIO and DMA memory.
6. **Namespace → block device.** Replace `nvme_block_register_deferred` with a
   real `nvme_block_register` that fills a `struct block_device` (name like
   `nvmeN`, `sector_count` and `sector_size` from Identify Namespace, `read`/
   `write` issuing I/O Read/Write) and calls `block_register_device`. The
   partition scanner (`partition_scan_all`) will then pick it up automatically and
   register `nvmeNpM` children, exactly as it does for AHCI disks.
7. **Persist the controller.** Replace the stack-local `struct nvme_controller`
   in `nvme_probe` with a heap-allocated, retained controller so the block-device
   ops can reach `regs`, `doorbell_stride`, and the queues.
8. **Interrupts.** NVMe is MSI/MSI-X-native; the PCI layer already programs MSI-X
   (`docs/pci_deep.md`), so I/O completion could be interrupt-driven and feed the
   block request queue (`docs/block_layer.md`).
