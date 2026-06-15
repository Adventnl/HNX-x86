# USB Core

The USB core (`kernel/usb/core/`) is the host-controller-agnostic device model
and enumeration engine. A host controller (xHCI) registers a `struct usb_bus`
that supplies the default control pipe and endpoint-configuration hooks; the
core fetches descriptors, selects a configuration, and offers the resulting
devices to USB class drivers (HID).

## Files

| File | Responsibility |
|------|----------------|
| `usb.h` | Core model: device, bus, driver, endpoint/interface/config |
| `usb.c` | Bus + driver registries, driver matching |
| `usb_device.c` | Static device pool + registry iteration |
| `usb_descriptor.c` | Descriptor structs + configuration-blob parser |
| `usb_request.c` | Standard requests + the enumeration sequence |
| `usb_transfer.c` | Synchronous control pipe wrapper |
| `usb_endpoint.c` | Endpoint helpers (direction, DCI, find interrupt-IN) |
| `usb_config.c` | Configuration selection helpers |
| `usb_hub.c` | Root-hub model (port count + per-port connection/speed) |

## Model

```
struct usb_bus      → a host controller (control pipe, EP config, interrupt I/O)
struct usb_device   → an enumerated device (address, slot, descriptors, config)
struct usb_configuration / usb_interface / usb_endpoint → parsed descriptors
struct usb_driver   → a class driver (probe() claims devices)
struct usb_transfer → a descriptive transfer record
```

## Enumeration (`usb_enumerate_device`)

xHCI assigns the device address in hardware (Address Device command), so the
core does **not** issue SET_ADDRESS; it records the logical address and then:

1. `GET_DESCRIPTOR(device, 8)` — learn `bMaxPacketSize0`.
2. `GET_DESCRIPTOR(device, 18)` — vendor/product/class/numConfigurations.
3. `GET_DESCRIPTOR(config, 9)` — read `wTotalLength`.
4. `GET_DESCRIPTOR(config, wTotalLength)` — the full configuration blob.
5. `usb_parse_config()` — extract the first interface + its endpoints.
6. `SET_CONFIGURATION` — activate the configuration.

A `HW_EVENT_USB_DEVICE_ATTACHED` event is emitted on success.

## Descriptor parser

`usb_parse_config()` walks the configuration blob descriptor-by-descriptor
(bounded by each `bLength`), capturing the first interface descriptor and its
endpoint descriptors into `struct usb_configuration`. It tolerates interleaved
class descriptors (e.g. the HID descriptor between the interface and endpoint).
The same parser is unit-tested against a hand-built boot-keyboard blob
(`[PASS] USB descriptor parser`).

## Hub foundation

`usb_hub.c` models the controller's root hub explicitly: a `struct usb_hub`
tracks each port's connection state and speed. External (downstream) hubs share
the same structure and are a future extension. Marker:
`[OK] USB hub foundation online`.

## Markers

```
[OK] USB core online
[PASS] USB descriptor parser
[OK] USB hub foundation online
[PASS] usb enumeration
```

`make verify-usb` checks these. On QEMU two devices enumerate (usb-kbd, usb-mouse,
both QEMU vendor `0x0627`).

## Userland

`lsusb`, `usbinfo`, `hidinfo` read the device list through `SYS_USB_DEVICES`;
`usbtest` asserts at least one device enumerated with a valid descriptor.
