# PCI Capabilities + MSI / MSI-X

`kernel/msi/` extends the PCI stack with capability-list parsing and an MSI /
MSI-X foundation.

## Files

| File | Responsibility |
|------|----------------|
| `pci_caps.c` | Capability-list walk (`pci_find_capability`, dump) |
| `msi.c` | MSI capability programming + subsystem init |
| `msix.c` | MSI-X table mapping + vector programming |

## Capability parsing

`pci_find_capability(dev, id)` walks the standard capability list (Status bit 4
→ pointer at `0x34` → linked `next` bytes), returning the config offset of the
requested capability. The legacy CF8/CFC window reaches the first 256 bytes,
where every standard capability lives (MSI `0x05`, MSI-X `0x11`, PCIe `0x10`,
Power-Management `0x01`). PCIe **extended** capabilities (offset ≥ `0x100`)
require ECAM/MMCONFIG, which MyOS does not map yet —
`pci_find_extended_capability()` reports this honestly (`-1`) rather than faking
a hit.

## MSI

On x86 an MSI is a posted write to `0xFEE00000 | (dest_apic << 12)` carrying the
target IDT vector as data. `msi_enable(dev, vector)` programs the message address
+ data (handling the 64-bit-address capability form) and sets the enable bit;
`msi_disable()` clears it. This is a **foundation**: the path is real and a
round-trip (`enable → read-back → disable`) is exercised by the self-test, but
the controllers currently run polled, so no live MSI vector is armed.

## MSI-X

The MSI-X vector table lives in an MMIO BAR. `msix_map_table(dev)` resolves the
Table BIR + offset, maps the BAR (`vmm_map_mmio_2m`), and caches the table
pointer. `msix_enable_vector()` writes a table entry (address/data) and unmasks
it. `msix_table_size()` reports the vector count. The self-test maps the table of
a real MSI-X-capable function (AHCI / NVMe / xHCI on QEMU).

## Markers

```
[OK] PCI capabilities parsed
[OK] MSI foundation online
[OK] MSI-X foundation online
[PASS] msi capability tests
```

`make verify-msi` checks these. Boot also logs how many PCI functions are
MSI- / MSI-X-capable.

## Userland

`msiinfo` lists each PCI function's MSI/MSI-X capability and table size via
`SYS_MSI_INFO`; `/tests/msi_test.hxe` asserts at least one capable function.

## Status

MSI/MSI-X is **functional as a foundation**: capabilities are parsed and
programmable and the MSI-X table is mapped from real MMIO. Routing live device
interrupts through MSI is deferred until the controllers move off the polled
model.
