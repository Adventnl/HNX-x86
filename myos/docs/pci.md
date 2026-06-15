# PCI Subsystem

Legacy **CF8/CFC** configuration mechanism #1 (`pci_config.c`):
`pci_config_read32(bus,slot,func,offset)` writes the 0x80000000-tagged address to
port 0xCF8 and reads 0xCFC; 16/8-bit accessors shift the dword.

`pci_scan_all()` walks bus 0–255 × slot 0–31 × func 0–7. For each present
function (vendor != 0xFFFF) it records vendor/device, class/subclass/prog_if,
revision, header type, the six BARs, and the interrupt line/pin into a flat
table. Multi-function devices (header type bit 7) are fully scanned.

Lookup helpers: `pci_find_by_class(class,sub)`,
`pci_find_by_class_prog(...)`, `pci_find_by_id(vendor,device)`,
`pci_device_at(i)`. `pci_device_bar(dev,idx,&is_mmio)` decodes I/O vs memory
BARs and 64-bit memory BARs. `pci_device_enable()` sets I/O + memory + bus-master
in the command register.

**Driver matching** (`pci_driver.c`): drivers register a `struct pci_driver`
(class/subclass[/prog_if] + `probe`); `pci_driver_match_all()` probes every
function against them. AHCI and NVMe register here.

Discovered functions are mirrored into the driver-core registry as
`DEV_TYPE_PCI` devices (`pci_register_devices`), so userspace `lspci`/`devices`
can enumerate them.

Markers: `[OK] PCI bus scanned`, `[PASS] pci enumeration`.
Tool: `tools/pci/pci_ids_min.py` (class/vendor name lookup).

## Prompt 6 — capabilities + MSI/MSI-X

`kernel/msi/pci_caps.c` adds capability-list parsing: `pci_find_capability(dev, id)` walks the Status-bit-4 capability list (pointer at 0x34, linked `next` bytes) to find MSI (0x05), MSI-X (0x11), PCIe (0x10), Power-Management (0x01). PCIe extended capabilities (offset >= 0x100) need ECAM and are reported honestly as unavailable. `kernel/msi/msi.c` + `msix.c` add the MSI/MSI-X foundation (message address/data programming, MSI-X table MMIO mapping). The xHCI controller is discovered here by class 0x0C / subclass 0x03 / prog-if 0x30. Markers: `[OK] PCI capabilities parsed`, `[OK] MSI foundation online`, `[OK] MSI-X foundation online`, `[PASS] msi capability tests`. See [msi.md](msi.md).
