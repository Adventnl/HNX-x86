# Hardware Compatibility

Prompt 6 builds MyOS's first serious hardware-compatibility layer: a reusable
device/driver lifecycle, an MSI/MSI-X foundation, a real xHCI USB host
controller, a USB core + HID stack, and a unified input model that merges PS/2
and USB input. This document is the compatibility overview + verification matrix.

## Subsystems

| Subsystem | Module | Status | Doc |
|-----------|--------|--------|-----|
| PCI capabilities | `kernel/msi/pci_caps.c` | working | [msi.md](msi.md) |
| MSI / MSI-X | `kernel/msi/` | foundation | [msi.md](msi.md) |
| Driver lifecycle | `kernel/driver/driver_lifecycle.c` | working | [driver_lifecycle.md](driver_lifecycle.md) |
| Power / reset | `kernel/hw/power/` | working | [driver_lifecycle.md](driver_lifecycle.md) |
| Hardware event bus | `kernel/hw/events/` | working | [driver_lifecycle.md](driver_lifecycle.md) |
| xHCI controller | `kernel/usb/xhci/` | working | [xhci.md](xhci.md) |
| USB core | `kernel/usb/core/` | working | [usb.md](usb.md) |
| USB HID | `kernel/usb/hid/` | working | [hid.md](hid.md) |
| Unified input | `kernel/input/` | working | [input.md](input.md) |

## Device compatibility (QEMU `q35`)

| Device | QEMU model | Result |
|--------|------------|--------|
| xHCI controller | `qemu-xhci` (1B36:000D) | discovered, reset, started |
| USB keyboard | `usb-kbd` | enumerated, boot protocol, online |
| USB mouse | `usb-mouse` | enumerated, boot protocol, online |
| PS/2 keyboard | i8042 | still works (unified) |
| AHCI / NVMe | `ich9-ahci` / `nvme` | MSI/MSI-X capabilities parsed |

The xHCI driver is real hardware bring-up (MMIO, reset, TRB rings, slot/endpoint
contexts, control transfers) — not a stub. Devices are honestly enumerated:
their descriptors are read from the controller over EP0, reporting QEMU's vendor
`0x0627`.

## Verification matrix

| Target | What it checks |
|--------|----------------|
| `make verify-msi` | PCI capability parse + MSI/MSI-X foundation |
| `make verify-driver-lifecycle` | lifecycle state machine + event bus |
| `make verify-xhci` | controller bring-up + root hub scan |
| `make verify-usb` | core + descriptor parser + hub + enumeration |
| `make verify-hid` | HID core + keyboard/mouse online + report tests |
| `make verify-input-unified` | PS/2 + USB keyboard + mouse + TTY unification |
| `make verify-hw-userland` | hwinfo/drivers/devtree/lsusb/hidinfo/inputtest |
| `make verify-prompt6` | all of the above + `verify-prompt5` + memory matrix |
| `make verify-qemu-matrix` | boots at 128M/256M/512M/1024M/2048M |

Each verify target boots the image headlessly in QEMU + OVMF and greps the COM1
serial log for required markers. Markers are emitted only on real success — no
hardcoded output.

## Userland tools

| Tool | Source | Data source |
|------|--------|-------------|
| `hwinfo` | `user/hw/hwinfo.c` | `SYS_HW_INFO` |
| `drivers` | `user/hw/drivers.c` | `SYS_DEVICES` (driver+state) |
| `devtree` | `user/hw/devtree.c` | `SYS_DEVICES` + `SYS_USB_DEVICES` |
| `interrupts` | `user/hw/interrupts.c` | `SYS_INTERRUPTS` |
| `msiinfo` | `user/hw/msiinfo.c` | `SYS_MSI_INFO` |
| `powerinfo` | `user/hw/powerinfo.c` | `SYS_DEVICES` (power) |
| `lsusb` / `usbinfo` / `hidinfo` | `user/usb/` | `SYS_USB_DEVICES` |
| `keytest` / `mousetest` / `inputtest` | `user/input/` | `SYS_INPUT_POLL` / `SYS_MOUSE_POLL` |

## Offline tooling

- `tools/usb/decode_usb_descriptor.py` — decode a descriptor blob.
- `tools/usb/decode_hid_report.py` — decode a HID report descriptor.
- `tools/hw/inspect_devices.py` — summarize a boot log's device inventory.
- `tools/hw/inspect_interrupts.py` — summarize interrupt-controller bring-up.

## Explicitly deferred

USB mass storage, networking, GUI, audio, SMP, ACPI power management, full
users/groups, package manager, compiler. USB mass storage in particular waits
until the USB core is stable.
