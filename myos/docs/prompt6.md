# Prompt 6 — USB and Hardware Compatibility Mega-Phase

Prompt 6 builds MyOS's first serious USB and hardware-compatibility layer on top
of the Prompt 1–5 foundation, entirely from scratch (no Linux/BSD/Windows USB or
HID code, no external libraries).

## What was built

1. **PCI capability parsing + MSI/MSI-X foundation** — `kernel/msi/`
   ([msi.md](msi.md)).
2. **Driver lifecycle + power/reset + hardware event bus** — `kernel/driver/`,
   `kernel/hw/` ([driver_lifecycle.md](driver_lifecycle.md)).
3. **xHCI host controller** — `kernel/usb/xhci/` ([xhci.md](xhci.md)): real MMIO
   bring-up, reset, command/event rings, device/input contexts, control + interrupt
   transfers, root-hub scan, device addressing.
4. **USB core** — `kernel/usb/core/` ([usb.md](usb.md)): device model, descriptor
   parser, enumeration, configuration selection, root-hub model.
5. **USB HID** — `kernel/usb/hid/` ([hid.md](hid.md)): boot keyboard + mouse.
6. **Unified input stack** — `kernel/input/` ([input.md](input.md)): one event
   model merging PS/2 and USB keyboards (text → TTY) and USB mouse (event queue).
7. **Userland HW/USB/input tools** + six new syscalls.
8. **Verification matrix** + **Python tooling** + **docs**.

## Boot integration

`kernel_main` (`kernel/src/kernel.c`) gains a Prompt 6 section after the Prompt 5
self-tests:

```
msi_init(); msi_tests_run();
driver_lifecycle_init(); hw_event_bus_init(); driver_tests_run();
xhci_init(); xhci_tests_run();
usb_core_init(); usb_hub_init(); xhci_attach_usb(); usb_tests_run();
hid_init(); usb_match_drivers(); unified_input_init();
hid_tests_run(); input_compat_tests_run();
```

The kernel version string is bumped to `MyOS Kernel 0.0.6`.

## New syscalls

`SYS_USB_DEVICES`, `SYS_HW_INFO`, `SYS_INTERRUPTS`, `SYS_INPUT_POLL`,
`SYS_MOUSE_POLL`, `SYS_MSI_INFO` (numbers 23–28). `struct sys_device_entry` is
extended with lifecycle state, power state and bound-driver name.

## Verification

```
make verify-msi              make verify-hid
make verify-driver-lifecycle make verify-input-unified
make verify-xhci             make verify-hw-userland
make verify-usb              make verify-prompt6   (everything + matrix)
```

All markers are emitted only on real success; the xHCI/USB/HID stack genuinely
enumerates QEMU's `usb-kbd` and `usb-mouse` over real control transfers.

## Line count

| Category | Lines |
|----------|-------|
| bootloader | 1,181 |
| kernel | 15,612 |
| user | 2,333 |
| shared | 56 |
| tools | 1,707 |
| docs | 1,900+ |
| **total** | **~22,800** |

New Prompt 6 kernel code: MSI ≈ 370, hw ≈ 300, USB ≈ 2,340, new input ≈ 220.

## Result vs. requirements

| Question | Answer |
|----------|--------|
| Does xHCI work? | Yes — real bring-up, No-Op + enumeration verified on QEMU |
| USB keyboard? | Yes — enumerated, boot protocol, online |
| USB mouse? | Yes — enumerated, boot protocol, online |
| PS/2 still works? | Yes — unified path, `[PASS] ps2 keyboard still works` |
| MSI/MSI-X? | Foundational — parsed + programmable + MSI-X table mapped |

## Next milestone — Prompt 7

Networking mega-phase: PCI NIC drivers, Ethernet, ARP, IPv4, ICMP ping, UDP,
DHCP, DNS, TCP foundation, sockets API, network userland tools, and a network
verification matrix.
