# PCI Subsystem (Deep Dive)

This document covers the HNX/MyOS PCI stack end to end: configuration-space access
over the legacy CF8/CFC ports, bus enumeration, BAR decoding, the class-based
driver matcher, capability-list parsing, and the MSI / MSI-X foundations. It is
grounded in `kernel/pci/` and `kernel/msi/`.

The stack is **mechanism #1 only** (CF8/CFC port I/O), which reaches the first
256 bytes of each function's config space. That is enough for every standard
capability (MSI, MSI-X, PCIe, Power Management). PCIe *extended* capabilities
(offset ≥ 0x100) need an ECAM/MMCONFIG window that MyOS does not map yet, and the
code reports that honestly rather than faking a result.

## Architecture

```
   x86_outl/inl  ->  CF8 (address) / CFC (data)        kernel/pci/pci_config.c
                          |
            pci_config_read{8,16,32} / write{16,32}
                          |
   pci_scan_all: bus 0..255 / slot 0..31 / func 0..7   kernel/pci/pci.c
                          |
            g_devices[PCI_MAX_DEVICES] (flat table, 64)
                  /              |               \
   pci_find_by_* (lookup)   pci_register_devices   pci_driver_match_all
                                |                        |
                     driver-core DEV_TYPE_PCI     class/subclass/prog_if match
                                                         |
                                          AHCI probe / NVMe probe / xHCI probe
                                                         |
                          pci_device_enable (IO|MEM|MASTER), pci_device_bar
                                                         |
                       capabilities: pci_find_capability  kernel/msi/pci_caps.c
                                          |               |
                                        MSI            MSI-X
                                  kernel/msi/msi.c   kernel/msi/msix.c
```

Enumeration produces a flat table of every present function. There are then two
consumers:

1. `pci_register_devices()` mirrors each function into the driver core as a
   `DEV_TYPE_PCI` `struct device` (so `lspci`/`devices` can list them).
2. `pci_driver_match_all()` runs the **PCI-specific** matcher: it pairs each
   function against registered `struct pci_driver`s by class/subclass (and
   optionally prog_if) and calls the winner's `probe`.

Note these are two separate driver registries. The generic driver core
(`docs/driver_model.md`) matches by `device_type`; the PCI matcher
(`pci_driver.c`) matches by PCI class triplet. Storage drivers register with the
PCI matcher.

### Boot wiring (`kernel/src/kernel.c`)

```c
driver_core_init();
pci_init();                   /* pci_scan_all + "[OK] PCI bus scanned" + count */
pci_register_devices();       /* mirror into driver core */
block_init();
ioapic_init();
ahci_init();                  /* pci_driver_register(&g_ahci_driver) */
nvme_init();                  /* pci_driver_register(&g_nvme_driver) */
pci_driver_match_all();       /* probe functions -> AHCI claims its controller */
...
msi_init();                   /* parse caps on every function; MSI/MSI-X markers */
msi_tests_run();
```

## File map

| File | Role |
| --- | --- |
| `kernel/pci/pci_config.h` / `pci_config.c` | CF8/CFC config mechanism #1: `make_address`, `pci_config_read{8,16,32}`/`write{16,32}`. |
| `kernel/pci/pci.h` / `pci.c` | Enumeration, the flat device table, `pci_find_by_*`, `pci_register_devices`, `pci_dump_devices`. |
| `kernel/pci/pci_device.h` / `pci_device.c` | `struct pci_device`; `pci_device_bar` (BAR decode incl. 64-bit), `pci_device_enable`. |
| `kernel/pci/pci_driver.h` / `pci_driver.c` | `struct pci_driver`, `pci_driver_register`, `pci_driver_match_all`, `matches`. |
| `kernel/pci/pci_ids.h` / `pci_ids.c` | Hand-curated class/subclass + vendor name tables. |
| `kernel/msi/pci_caps.h` / `pci_caps.c` | Capability IDs, `pci_find_capability`, `pci_find_extended_capability` (honest -1), `pci_dump_capabilities`. |
| `kernel/msi/msi.h` / `msi.c` | MSI capability programming + `msi_init` (subsystem markers). |
| `kernel/msi/msix.h` / `msix.c` | MSI-X table mapping + per-vector programming. |
| `kernel/tests/pci_tests.c` | `pci_tests_run`: enumeration smoke test. |
| `kernel/tests/msi_tests.c` | `msi_tests_run`: capability walk + MSI/MSI-X round-trips. |

## Configuration space access (CF8/CFC)

`pci_config.c` implements PCI configuration mechanism #1:

```c
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return 0x80000000u                       /* enable bit */
         | ((uint32_t)bus  << 16)
         | ((uint32_t)(slot & 0x1F) << 11)
         | ((uint32_t)(func & 0x07) <<  8)
         | ((uint32_t)offset & 0xFC);        /* dword-aligned */
}
```

- `pci_config_read32` writes the address to 0xCF8, reads a dword from 0xCFC.
- `pci_config_read16`/`read8` read the aligned dword and shift/mask out the
  sub-field (`(offset & 2) * 8` for 16-bit, `(offset & 3) * 8` for 8-bit).
- `pci_config_write32` is a straight dword write.
- `pci_config_write16` is **read-modify-write**: it reads the dword, clears the
  16-bit lane at `(offset & 2) * 8`, OR's in the new value, and writes back
  (re-asserting the address before the data write). This matters for safely
  flipping bits in the command/status/MSI-control registers without clobbering the
  adjacent 16 bits.

All four port operations go through `x86_outl`/`x86_inl` (`cpu.h`).

## Enumeration

`pci_scan_all` is a brute-force scan of the full topology:

```c
for bus in 0..255:
  for slot in 0..31:
    if vendor(bus,slot,0) == 0xFFFF: continue        /* no function 0 -> empty slot */
    read_function(bus, slot, 0)
    if header_type(bus,slot,0) & 0x80:               /* multi-function */
        for func in 1..7: read_function(bus, slot, func)
```

`read_function` reads vendor at offset 0x00 and bails on `0xFFFF` (no function).
Otherwise it records a `struct pci_device` from config space:

| Field | Offset | Notes |
| --- | --- | --- |
| `vendor` | 0x00 | 16-bit |
| `device` | 0x02 | 16-bit |
| `revision` | 0x08 | |
| `prog_if` | 0x09 | |
| `subclass` | 0x0A | |
| `class_code` | 0x0B | base class |
| `header_type` | 0x0E | bit 7 = multi-function |
| `bar[0..5]` | 0x10 + i*4 | raw 32-bit BAR dwords |
| `irq_line` | 0x3C | |
| `irq_pin` | 0x3D | |

The table is capped at `PCI_MAX_DEVICES` (64); functions past 64 are dropped.
`pci_init` runs the scan and logs `[OK] PCI bus scanned` plus the function count.

## Data structures

### `struct pci_device` (`pci_device.h`)

```c
struct pci_device {
    uint8_t  bus, slot, func;
    uint16_t vendor, device;
    uint8_t  class_code, subclass, prog_if, revision;
    uint8_t  header_type;
    uint8_t  irq_line, irq_pin;
    uint32_t bar[6];
    uint8_t  in_use;
};
```

### `struct pci_driver` (`pci_driver.h`)

```c
struct pci_driver {
    const char *name;
    uint8_t     class_code;
    uint8_t     subclass;
    uint8_t     prog_if;
    uint8_t     match_prog_if;     /* 1 = prog_if must also match */
    int (*probe)(const struct pci_device *dev);
    struct pci_driver *next;
};
```

Example (AHCI, from `ahci.c`):

```c
static struct pci_driver g_ahci_driver = {
    .name = "ahci", .class_code = 0x01, .subclass = 0x06,
    .prog_if = 0x01, .match_prog_if = 1, .probe = ahci_probe,
};
```

NVMe sets `class_code = 0x01, subclass = 0x08, prog_if = 0x02` but
`match_prog_if = 0` (match any NVMe prog_if).

## Key APIs

### Enumeration + lookup (`pci.h`)

```c
void pci_init(void);                    /* scan + "[OK] PCI bus scanned" */
void pci_scan_all(void);

const struct pci_device *pci_find_by_class(uint8_t class_code, uint8_t subclass);
const struct pci_device *pci_find_by_class_prog(uint8_t cc, uint8_t sc, uint8_t prog_if);
const struct pci_device *pci_find_by_id(uint16_t vendor, uint16_t device);

int                      pci_device_count(void);
const struct pci_device *pci_device_at(int index);
void                     pci_dump_devices(void);
void                     pci_register_devices(void);     /* mirror into driver core */
```

`pci_register_devices` builds a name like `pci<bb>:<ss>.<f>` (hex bus:slot.func,
e.g. `pci00:1f.2`), fills `struct device_id` (vendor/device/class/subclass/
prog_if), points `bus_data` at the `pci_device`, and calls `device_register`.

### BARs + enable (`pci_device.h`)

```c
uint64_t pci_device_bar(const struct pci_device *dev, int idx, int *is_mmio);
void     pci_device_enable(const struct pci_device *dev);
```

`pci_device_bar` decodes BAR `idx` (0–5):
- Bit 0 set → I/O BAR: `*is_mmio = 0`, base is `bar & ~0x3`.
- Bit 0 clear → memory BAR: `*is_mmio = 1`, base is `bar & ~0xF`. The type field
  `(bar >> 1) & 0x3 == 0x2` means a 64-bit BAR, so the high half is taken from
  `bar[idx + 1] << 32` (consuming the next slot, only if `idx < 5`).
- Out-of-range idx → returns 0, `*is_mmio = 0`.

`pci_device_enable` (command register at 0x04) OR's in `PCI_CMD_IO (1<<0)`,
`PCI_CMD_MEM (1<<1)`, `PCI_CMD_MASTER (1<<2)` — enabling I/O space, memory space,
and bus mastering. AHCI/NVMe probes call this before touching their BARs.

### Driver matcher (`pci_driver.h`)

```c
void pci_driver_register(struct pci_driver *drv);   /* prepend to g_drivers */
int  pci_driver_match_all(void);                    /* probe all funcs, return bind count */
```

`matches(drv, dev)` requires `class_code` and `subclass` to be equal, and — only
if `drv->match_prog_if` — also `prog_if`. `pci_driver_match_all` iterates every
function, and for each tries every registered driver; the first whose `matches`
holds and whose `probe(dev) == 0` claims it (`break`), incrementing `bound`.

### Names (`pci_ids.h`)

```c
const char *pci_class_name(uint8_t class_code, uint8_t subclass);
const char *pci_vendor_name(uint16_t vendor);
```

A small hand-curated subset (mirrors `tools/pci/pci_ids_min.py`). Storage
subclasses are named: `0x01/0x00` scsi, `0x01` ide, `0x06` sata-ahci, `0x08`
nvme. Known vendors include Intel (0x8086), AMD (0x1022), QEMU/Bochs (0x1234),
Red Hat/Virtio (0x1AF4 / 0x1B36), Realtek (0x10EC), VMware (0x15AD).

## Capability parsing

`pci_caps.c` walks the standard capability linked list, which lives within the
first 256 bytes and is therefore reachable over CF8/CFC.

```c
#define PCI_CAP_ID_PM    0x01   /* Power Management */
#define PCI_CAP_ID_MSI   0x05
#define PCI_CAP_ID_VNDR  0x09
#define PCI_CAP_ID_PCIE  0x10
#define PCI_CAP_ID_MSIX  0x11

#define PCI_STATUS_REG       0x06
#define PCI_STATUS_CAP_LIST  0x0010   /* status bit 4 */
#define PCI_CAP_PTR_REG      0x34
```

`pci_find_capability`:
1. Read status (0x06); if bit 4 (`PCI_STATUS_CAP_LIST`) is clear, return -1 (no
   list).
2. Read the capability pointer at 0x34, masked with `& 0xFC` (dword-aligned).
3. Walk: at each `ptr`, read the cap ID byte. `0xFF` ends the list. If it matches
   the requested ID, return the offset. Otherwise advance to `*(ptr+1) & 0xFC`
   (the next-pointer). A `guard < 48` loop bound prevents a malformed circular
   list from hanging (256/4 = 64 max entries; 48 is a safe cap given alignment).

`pci_find_extended_capability` is honest: PCIe extended caps (offset ≥ 0x100)
need ECAM, which is not mapped, so it always returns -1. `pci_dump_capabilities`
walks the same list and logs each cap by name (`power-management`, `msi`,
`vendor-specific`, `pci-express`, `msi-x`, or `capability`).

## MSI (`msi.c`)

On x86 an MSI is a posted memory write to `0xFEE00000 | (dest_apic << 12)`
carrying the target IDT vector as data. The MSI capability register offsets
(relative to the capability base) are:

```c
#define MSI_CTRL          0x02     /* 16-bit message control */
#define MSI_ADDR_LO       0x04     /* message address (low)  */
#define MSI_CTRL_ENABLE   0x0001   /* bit 0 */
#define MSI_CTRL_64BIT    0x0080   /* bit 7: 64-bit address capable */
#define MSI_CTRL_MME_MASK 0x0070   /* bits 4-6: multiple message enable */
#define MSI_ADDR_BASE     0xFEE00000u
```

```c
int  msi_supported(struct pci_device*);                 /* cap present? */
int  msi_is_enabled(struct pci_device*);                /* 1/0, or -1 if unsupported */
int  msi_enable(struct pci_device*, uint8_t vector);
void msi_disable(struct pci_device*);
void msi_init(void);
```

`msi_enable`:
1. Find the MSI cap; -1 if absent.
2. Read control; check bit 7 for 64-bit addressing.
3. Compute `addr = 0xFEE00000 | (bsp_apic_id() << 12)` (BSP APIC ID from LAPIC
   reg 0x20 bits 24–31) and `data = vector` (fixed delivery, edge-triggered).
4. Write address low at `cap+0x04`. For 64-bit caps, write address high (0) at
   `cap+0x08` and data at `cap+0x0C`; for 32-bit, write data at `cap+0x08`.
5. Clear `MME` (one vector) and set `ENABLE` in control.

`msi_disable` clears just the enable bit. `msi_is_enabled` reads control bit 0.

## MSI-X (`msix.c`)

MSI-X keeps its vector table in an MMIO BAR, allowing many independent vectors per
function. Register/entry layout:

```c
#define MSIX_CTRL          0x02     /* message control */
#define MSIX_TABLE         0x04     /* table offset / BIR */
#define MSIX_CTRL_ENABLE   0x8000   /* bit 15 */
#define MSIX_CTRL_FUNCMASK 0x4000   /* bit 14 */
#define MSIX_CTRL_SIZE     0x07FF   /* bits 0-10 = table size minus one */

#define MSIX_ENTRY_BYTES   16
#define MSIX_ENTRY_ADDR_LO 0x00
#define MSIX_ENTRY_ADDR_HI 0x04
#define MSIX_ENTRY_DATA    0x08
#define MSIX_ENTRY_VCTRL   0x0C
#define MSIX_VCTRL_MASK    0x00000001u
```

```c
int  msix_supported(struct pci_device*);
int  msix_table_size(struct pci_device*);                /* (ctrl & 0x7FF) + 1, or -1 */
int  msix_map_table(struct pci_device*);
int  msix_enable_vector(struct pci_device*, uint16_t table_index, uint8_t vector);
void msix_disable(struct pci_device*);
```

`msix_map_table`:
1. Find the MSI-X cap; read the table dword at `cap+0x04`. Low 3 bits are the
   **BIR** (which BAR holds the table); the rest (`& ~0x7`) is the table offset
   within that BAR.
2. Resolve the BAR via `pci_device_bar`; require it to be MMIO and non-zero.
3. `table_phys = bar + offset`; map it with `vmm_map_mmio_2m(table_phys &
   ~LARGE_PAGE_MASK)` (2 MiB MMIO page).
4. Record the mapping in a small fixed cache (`g_maps[8]`, keyed by bus/slot/func)
   with the table pointer and entry count.

`msix_enable_vector` writes one 16-byte table entry: address low =
`0xFEE00000 | (bsp_apic_id << 12)`, address high = 0, data = `vector`, then clears
the vector-control mask bit (unmask). It then clears function-mask and sets the
MSI-X enable bit in the capability control register. Requires `msix_map_table`
to have run first (lookup of the cached mapping fails otherwise).

### `msi_init`

Iterates every function, counting those with a capability list
(`PCI_STATUS_CAP_LIST`), those MSI-capable, and those MSI-X-capable, then emits:

```
[OK] PCI capabilities parsed
    functions w/ caps : <n>
[OK] MSI foundation online
    msi-capable funcs : <n>
[OK] MSI-X foundation online
    msix-capable funcs: <n>
```

## Invariants

- **Mechanism #1, first 256 bytes only.** Every config access is CF8/CFC; the
  capability walk is bounded to standard caps. Extended caps are unreachable and
  reported as -1, never faked.
- **Config addresses are dword-aligned.** `make_address` masks `offset & 0xFC`;
  sub-dword reads/writes shift within the aligned dword.
- **16-bit config writes are read-modify-write.** Protects the adjacent 16-bit
  lane in shared registers (command, MSI control).
- **Function 0 gates the slot.** If function 0's vendor is `0xFFFF`, the slot is
  empty and higher functions are not probed; higher functions are only scanned if
  header type bit 7 (multi-function) is set.
- **Table is capped at 64 functions** (`PCI_MAX_DEVICES`); the MSI-X mapping cache
  at 8 functions (`g_maps[8]`).
- **Capability walk is loop-guarded.** `guard < 48` and the `0xFF` terminator
  prevent a malformed list from hanging the walker.
- **64-bit BARs consume two slots.** `pci_device_bar` only reads the high half
  when `idx < 5`; a 64-bit BAR at slot 5 would have no high half to read.
- **MSI targets the BSP LAPIC.** Both MSI and MSI-X program
  `0xFEE00000 | (bsp_apic_id << 12)` with fixed, edge-triggered delivery.

## Failure modes

- **Empty slot / absent function →** vendor reads `0xFFFF`; skipped during scan;
  `pci_device_bar` returns 0 for an unpopulated BAR.
- **More than 64 functions →** extra functions silently dropped by `read_function`.
- **No capability list →** `pci_find_capability` returns -1; `msi_supported`/
  `msix_supported` return 0; `msi_is_enabled` returns -1.
- **BAR not MMIO / unresolved →** `msix_map_table` returns -1; the MSI-X table is
  not mapped and `msix_enable_vector` will fail its lookup.
- **MMIO map failure →** `vmm_map_mmio_2m != 0` makes `msix_map_table` return -1.
- **MSI-X cache full (>8 mapped functions) →** `alloc_slot` returns 0,
  `msix_map_table` returns -1.
- **PCIe extended capability requested →** `pci_find_extended_capability` returns
  -1 unconditionally (no ECAM).
- **No driver matches a function →** `pci_driver_match_all` simply does not bind
  it; the function still appears in the table and the driver-core mirror.

## Verification

```
make verify-pci     # enumeration came up and found devices
make verify-msi     # capability parse + MSI/MSI-X foundations + round-trip test
make verify-prompt5 # PCI is part of the Prompt 5 chain
make verify-prompt6 # MSI is part of the Prompt 6 chain
```

Serial markers and emitters:

| Marker | Emitter |
| --- | --- |
| `[OK] PCI bus scanned` | `pci_init` (`pci.c`) |
| `[PASS] pci enumeration` | `pci_tests_run` (`pci_tests.c`) |
| `[OK] PCI capabilities parsed` | `msi_init` (`msi.c`) |
| `[OK] MSI foundation online` | `msi_init` |
| `[OK] MSI-X foundation online` | `msi_init` |
| `[PASS] msi capability tests` | `msi_tests_run` (`msi_tests.c`) |

`verify-pci` expects `[OK] PCI bus scanned` and `[PASS] pci enumeration`. The
enumeration test (`pci_tests.c`) asserts there is at least one function (a PCI
machine always has a host bridge, `pci_find_by_class(0x06, 0x00)`).

`verify-msi` expects `[OK] PCI capabilities parsed`, `[OK] MSI foundation
online`, `[OK] MSI-X foundation online`, and `[PASS] msi capability tests`. The
MSI test (`msi_tests.c`) does a real round-trip against a live function: it
finds an MSI-capable function, `msi_enable(dev, 0x71)` then asserts
`msi_is_enabled == 1`, then `msi_disable` and asserts `== 0` (so no vector is
left armed); for MSI-X it asserts `msix_table_size > 0` and `msix_map_table == 0`.

The expanded suite (`Makefile.production`) adds `verify-pci-expanded` to
`verify-production-200k`.

## Future expansion

- **ECAM / MMCONFIG.** Mapping the MMCONFIG window would unlock PCIe extended
  config (offset ≥ 0x100) and make `pci_find_extended_capability` real (AER, SR-IOV,
  etc.). The current code is explicitly written to flip from -1 to a real walk.
- **BAR sizing.** Today BARs are read as-presented (assigned by firmware); a real
  resource manager would do the write-all-ones/read-back probe to size BARs and
  could reassign them.
- **MSI multiple-message.** `MSI_CTRL_MME_MASK` is cleared to one vector;
  multi-vector MSI is unimplemented. MSI-X already supports many vectors via the
  table.
- **Live interrupt routing.** MSI/MSI-X program the address/data pair, but routing
  a delivered vector into a registered handler is layered on by the controller
  drivers that opt in (e.g. xHCI); a general MSI vector allocator could centralize
  this.
- **Legacy INTx routing.** `irq_line`/`irq_pin` are captured but the stack relies
  on IOAPIC/MSI; full INTx routing via the ACPI _PRT is not wired.
- **Hot-plug.** Enumeration is a one-shot boot scan; a rescan path and bridge
  secondary-bus handling would support hot-plug and deeper topologies.
