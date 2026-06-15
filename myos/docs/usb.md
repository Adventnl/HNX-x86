# USB Core

The USB core (`kernel/usb/core/`) is the host-controller-agnostic device model
and enumeration engine. A host controller (xHCI) registers a `struct usb_bus`
whose function pointers supply the default control pipe and endpoint hooks; the
core uses those to fetch and parse descriptors, select a configuration, build the
interface/endpoint model, and hand devices to class drivers (HID).

It is brought up in `kernel_main()` after the controller is running:

```c
usb_core_init();      /* "[OK] USB core online"                    */
usb_hub_init();       /* "[OK] USB hub foundation online"          */
xhci_attach_usb();    /* controller registers a bus + enumerates   */
usb_tests_run();      /* descriptor-parser + live-enumeration self-test */
...
hid_init();           /* HID registers a usb_driver                */
usb_match_drivers();  /* probe every enumerated device             */
```

## Architecture

```
   xHCI (or any HCD)                  USB core                  class driver
   ----------------                   --------                  ------------
   struct usb_bus  ── usb_register_bus ──►  g_buses (list)
     .control                                  |
     .configure_endpoint                       | per port:
     .submit_interrupt                         |   usb_alloc_device
     .poll_interrupt                           |   dev->bus/hc_slot/port/speed
                                               |   usb_enumerate_device ──► usb_control ──► bus.control
                                               |        GET_DESCRIPTOR(device,8)
                                               |        GET_DESCRIPTOR(device,full)
                                               |        GET_DESCRIPTOR(config,9)
                                               |        GET_DESCRIPTOR(config,full)
                                               |        usb_parse_config
                                               |        SET_CONFIGURATION
                                               |   usb_register_device
                                               v
                                          g_devices[16]  ──► usb_match_drivers ──► driver.probe()
```

There is no SET_ADDRESS round-trip: xHCI assigns the address in hardware during
Address Device, so `usb_set_address()` only records the logical address in the
device. Enumeration is fully synchronous — every `usb_get_descriptor` blocks on
the bus's control transfer (which itself polls the event ring).

### Enumeration flow (`usb_enumerate_device`, `usb_request.c`)

1. `GET_DESCRIPTOR(DEVICE, 8)` — learn `bMaxPacketSize0` (`dev->max_packet0`).
2. `GET_DESCRIPTOR(DEVICE, 18)` — record `vendor_id`, `product_id`, class triple,
   `num_configs`.
3. `GET_DESCRIPTOR(CONFIG, 9)` — read `wTotalLength`.
4. `GET_DESCRIPTOR(CONFIG, total)` into `dev->raw_config` (`raw_config_len`).
5. `usb_parse_config(raw_config, total, &dev->config)` — fill interface +
   endpoints.
6. `SET_CONFIGURATION(config.value)`.
7. `hw_event_emit(HW_EVENT_USB_DEVICE_ATTACHED, vendor_id, product_id,
   "usb device enumerated")`.

### The control-transfer pipe

Every step above is a control transfer composed by `usb_request.c` and dispatched
through `usb_control` → `dev->bus->control`. The standard USB 8-byte SETUP packet
is assembled from the helper's arguments: `bmRequestType` combines a direction
(`USB_DIR_IN`/`USB_DIR_OUT`), a type (`USB_TYPE_STANDARD`/`USB_TYPE_CLASS`) and a
recipient (`USB_RECIP_DEVICE`/`USB_RECIP_INTERFACE`). `usb_get_descriptor` packs
the descriptor type into the high byte of `wValue` and the index into the low
byte, requests `USB_REQ_GET_DESCRIPTOR` IN from the device, and the xHCI bus
callback turns that into Setup/Data/Status TRBs on the EP0 ring.
`usb_set_configuration` issues `USB_REQ_SET_CONFIGURATION` OUT with the config
value in `wValue` and, on success, records `dev->config.value`. Because the data
stage rides the xHCI bounce page, the core never owns DMA buffers itself.

### Driver matching

`usb_register_driver` links a `struct usb_driver` into `g_drivers`.
`usb_match_drivers` iterates every enumerated device and calls each driver's
`probe(dev)`; the first driver whose probe returns 0 claims the device
(`dev->driver` set) and probing for that device stops. There is no VID/PID/class
match table yet — every driver inspects the device itself (the HID driver checks
`iface_class == 0x03`). `usb_dump_devices` and `usb_device_at`/`usb_device_count`
expose the registry to the self-tests and the userland `lsusb`/`usbinfo` tools.

## File map

| File | Responsibility |
|------|----------------|
| `kernel/usb/core/usb.h` | Core structs (`usb_device`, `usb_bus`, `usb_interface`, `usb_endpoint`, `usb_configuration`, `usb_driver`, `usb_transfer`), limit constants, core API. |
| `kernel/usb/core/usb.c` | `usb_core_init`, bus/device/driver registries, `usb_match_drivers`. |
| `kernel/usb/core/usb_device.c/.h` | Static device pool (`g_devices[USB_MAX_DEVICES]`), `usb_alloc_device`, `usb_device_at`, `usb_dump_devices`, `usb_speed_string`. |
| `kernel/usb/core/usb_request.c/.h` | Standard request constants and the request helpers (`usb_get_descriptor`, `usb_set_address`, `usb_set_configuration`, `usb_enumerate_device`). |
| `kernel/usb/core/usb_descriptor.c/.h` | Packed descriptor structs, descriptor-type constants, `usb_parse_config`. |
| `kernel/usb/core/usb_config.c/.h` | `usb_config_select`, `usb_device_interface`. |
| `kernel/usb/core/usb_endpoint.c/.h` | Endpoint helpers (direction/number/DCI, find interrupt-IN). |
| `kernel/usb/core/usb_transfer.c/.h` | `usb_control` — dispatch a control transfer to `dev->bus->control`. |
| `kernel/usb/core/usb_hub.c/.h` | Root-hub foundation (`usb_hub_register_root`, per-port state). |
| `kernel/tests/usb_tests.c` | Descriptor-parser unit test + live-enumeration check. |

## Data structures

### `struct usb_device` (`usb.h`)

| Field | Type | Meaning |
|-------|------|---------|
| `bus` | `struct usb_bus *` | Owning host controller. |
| `address` | `uint8_t` | Logical USB address (recorded, not negotiated). |
| `hc_slot` | `uint8_t` | xHCI slot ID. |
| `port` | `uint8_t` | Root-hub port number. |
| `speed` | `uint8_t` | 1=full, 2=low, 3=high, 4=super. |
| `vendor_id`, `product_id` | `uint16_t` | From the device descriptor. |
| `dev_class/dev_subclass/dev_protocol` | `uint8_t` | Device-level class triple. |
| `max_packet0` | `uint8_t` | EP0 max packet (`bMaxPacketSize0`). |
| `num_configs` | `uint8_t` | `bNumConfigurations`. |
| `config` | `struct usb_configuration` | Active config (one interface). |
| `driver`, `driver_data` | claiming driver + private state. |
| `raw_config[USB_RAW_CONFIG_MAX]`, `raw_config_len` | raw config blob. |
| `in_use` | `uint8_t` | pool slot flag. |

### `struct usb_bus` (`usb.h`) — the HCD interface

```c
struct usb_bus {
    const char *name;
    void       *hc;     /* e.g. struct xhci *                          */
    int (*control)(dev, bmRequestType, bRequest, wValue, wIndex, wLength, data);
    int (*configure_endpoint)(dev, ep_addr, max_packet, interval);
    int (*submit_interrupt)(dev, dma_buf, length);
    int (*poll_interrupt)(dev, int *bytes);
    struct usb_bus *next;
};
```

### Interface / endpoint / configuration (`usb.h`)

- `struct usb_endpoint { uint8_t address; uint8_t attributes; uint16_t max_packet;
  uint8_t interval; uint8_t in_use; }`.
- `struct usb_interface { uint8_t number; iface_class; iface_subclass;
  iface_protocol; num_endpoints; struct usb_endpoint endpoints[USB_MAX_ENDPOINTS]; }`.
- `struct usb_configuration { uint8_t value; num_interfaces; uint16_t
  total_length; struct usb_interface interface; }` — boot-device model: one
  interface kept.
- `struct usb_driver { const char *name; int (*probe)(struct usb_device *);
  struct usb_driver *next; }`.

### Packed descriptors (`usb_descriptor.h`)

`usb_device_descriptor` (18B), `usb_config_descriptor` (9B),
`usb_interface_descriptor` (9B), `usb_endpoint_descriptor` (7B), and
`usb_hid_descriptor` (the class descriptor with `bReportDescriptorType` /
`wReportDescriptorLength`). All `__attribute__((packed))` with the standard
field names (`bLength`, `bDescriptorType`, `wTotalLength`, `bEndpointAddress`,
`wMaxPacketSize`, etc.).

### `struct usb_hub` / `struct usb_hub_port` (`usb_hub.h`)

```c
struct usb_hub_port { uint8_t connected; uint8_t speed; uint8_t reset_done; };
struct usb_hub { const char *name; uint8_t num_ports; uint8_t is_root;
                 struct usb_hub_port ports[USB_HUB_MAX_PORTS]; };
```

## Key APIs

### Core (`usb.h` / `usb.c`)

- `void usb_core_init(void)` — logs `USB core online`.
- `int usb_register_bus(struct usb_bus *)` — link a HCD into `g_buses`.
- `struct usb_device *usb_alloc_device(void)` / `int usb_register_device(...)`.
- `int usb_enumerate_device(struct usb_device *)` — the flow above.
- `int usb_get_descriptor(dev, type, index, buffer, length)`.
- `int usb_set_address(dev, address)` — records `dev->address` only.
- `int usb_set_configuration(dev, value)` — SET_CONFIGURATION control request.
- `int usb_register_driver(struct usb_driver *)` / `void usb_match_drivers(void)`.
- `int usb_device_count(void)` / `struct usb_device *usb_device_at(int)`.
- `void usb_dump_devices(void)`.

### Requests (`usb_request.h`)

Constants: direction `USB_DIR_OUT`/`USB_DIR_IN` (0x80), type
`USB_TYPE_STANDARD`/`USB_TYPE_CLASS` (0x20), recipient
`USB_RECIP_DEVICE`/`USB_RECIP_INTERFACE`; requests `USB_REQ_GET_STATUS` (0),
`CLEAR_FEATURE` (1), `SET_FEATURE` (3), `SET_ADDRESS` (5), `GET_DESCRIPTOR` (6),
`SET_DESCRIPTOR` (7), `GET_CONFIGURATION` (8), `SET_CONFIGURATION` (9),
`SET_INTERFACE` (0x0B).

### Descriptors (`usb_descriptor.h`)

Types: `USB_DT_DEVICE` (1), `USB_DT_CONFIG` (2), `USB_DT_STRING` (3),
`USB_DT_INTERFACE` (4), `USB_DT_ENDPOINT` (5), `USB_DT_HID` (0x21),
`USB_DT_HID_REPORT` (0x22). Endpoint attributes:
`USB_EP_CONTROL`/`ISOCH`/`BULK`/`INTERRUPT` (0..3), `USB_EP_XFER_MASK` (0x03),
`USB_EP_DIR_IN` (0x80). API: `usb_parse_config(blob, len, *out)`,
`usb_descriptor_type_name(type)`.

### Endpoint helpers (`usb_endpoint.h`)

- `usb_endpoint_is_interrupt(ep)` — `(attributes & USB_EP_XFER_MASK) == USB_EP_INTERRUPT`.
- `usb_endpoint_is_in(ep)` — bit 7 of `address`.
- `usb_endpoint_number(ep)` — `address & 0x0F`.
- `usb_endpoint_dci(ep)` — xHCI Device Context Index `2*N + dir_in` (EP0 = 1).
- `usb_interface_interrupt_in(iface)` — first interrupt-IN endpoint, or NULL.

### Transfer / config / hub

- `int usb_control(dev, bmRequestType, bRequest, wValue, wIndex, wLength, data)`
  — dispatch to `dev->bus->control`.
- `int usb_config_select(dev, value)`, `struct usb_interface *usb_device_interface(dev)`.
- `void usb_hub_init(void)`, `struct usb_hub *usb_hub_register_root(name, num_ports)`,
  `void usb_hub_set_port(hub, port, connected, speed)`, `struct usb_hub *usb_hub_root(void)`,
  `uint8_t usb_hub_connected_count(hub)`.

## Invariants

- **Static pools, no dynamic allocation.** `USB_MAX_DEVICES` = 16 devices, each
  interface holds up to `USB_MAX_ENDPOINTS` = 8 endpoints, the raw config blob is
  capped at `USB_RAW_CONFIG_MAX` = 256 bytes, hubs at `USB_HUB_MAX_PORTS` = 16.
- **Boot-device model.** Only the first interface and its endpoints of the first
  configuration are retained in `dev->config`; multi-interface composite devices
  keep their raw blob but expose one interface.
- **HCD-agnostic.** The core never reads a register. Every transfer goes through
  `dev->bus->{control,submit_interrupt,...}`; a NULL bus or NULL callback yields
  an error rather than a fault.
- **Address recorded, not negotiated.** `usb_set_address` updates `dev->address`
  only; the hardware address was set by xHCI Address Device.
- **Synchronous enumeration.** All steps run on the boot thread before the
  scheduler starts; there is no concurrent re-enumeration path.
- **Sequential descriptor walk.** `usb_parse_config` advances strictly by each
  descriptor's `bLength` and stops cleanly on a malformed/truncated blob.

## Failure modes

All `int` APIs return 0 on success, -1 on failure. Notable cases:

- NULL argument guards: `usb_register_bus/device`, `usb_enumerate_device`,
  `usb_set_address`, `usb_control` (NULL dev/bus/callback), `usb_parse_config`
  (NULL blob/out), `usb_register_driver`.
- `usb_enumerate_device` logs and returns -1 at each failed step:
  `usb: GET_DESCRIPTOR(device, 8) failed`,
  `usb: GET_DESCRIPTOR(device, full) failed`,
  `usb: GET_DESCRIPTOR(config, 9) failed`,
  `usb: GET_DESCRIPTOR(config, full) failed`,
  `usb: config descriptor parse failed`, `usb: SET_CONFIGURATION failed`.
- `usb_parse_config` fails on a short blob, an out-of-range descriptor type, or
  no interface found.
- Pool exhaustion: `usb_alloc_device` returns NULL once 16 devices are in use.
- No panics — a failed enumeration leaves that port without a device; the
  `[PASS] usb enumeration` marker depends on at least one valid device.

## Verification

```
make verify-usb
```

asserts (markers emitted by code that actually ran):

```
[OK] USB core online
[PASS] USB descriptor parser
[OK] USB hub foundation online
[PASS] usb enumeration
```

`usb_tests_run` (`kernel/tests/usb_tests.c`) runs `usb_parse_config` over a
hand-built 34-byte boot-keyboard blob (config + interface class 3 / subclass 1 /
protocol 1 + HID descriptor + EP1-IN interrupt 8-byte endpoint) and asserts the
parsed interface class/subclass/protocol, endpoint count, and that
`usb_interface_interrupt_in` finds EP `0x81` with `max_packet == 8`. It then
checks the live root hub produced at least one device with a non-zero
`vendor_id` (`usb_device_count() > 0`, `usb_device_at(0)`), emitting
`[PASS] usb enumeration`. QEMU supplies the devices via
`-device usb-kbd,bus=xhci.0` and `-device usb-mouse,bus=xhci.0`. The expanded
suite (`make verify-usb-expanded` in `Makefile.production`) layers further USB
checks; `make verify-prompt6` runs the whole USB/HID/input chain.

## Notes on the device model

- **Speed encoding.** `dev->speed` mirrors the xHCI port-speed IDs (1=full,
  2=low, 3=high, 4=super); `usb_speed_string` maps them to `"full"`/`"low"`/
  `"high"`/`"super"`/`"?"` for the device dump and the userland `lsusb`.
- **Endpoint DCI.** `usb_endpoint_dci` computes the xHCI Device Context Index
  (`2*N + dir_in`), the bridge between the descriptor-level endpoint model and the
  controller's per-slot endpoint contexts. EP0 is always DCI 1.
- **One config blob retained.** `dev->raw_config` keeps the full configuration
  descriptor bytes (capped at 256) even though only the first interface is
  modeled, so a future multi-interface walk needs no re-fetch.

## Future expansion

- **String descriptors.** `USB_DT_STRING` is defined but no `usb_get_string`
  helper exists; product/manufacturer names are never fetched.
- **Multiple configurations and interfaces.** The boot-device model keeps one
  interface; composite devices (e.g. keyboard + consumer-control) need an
  interface array and per-interface driver binding.
- **External hub class driver.** `usb_hub` models ports but does not drive a real
  hub: it needs the hub descriptor, port power/reset over the hub's control pipe,
  and a status-change interrupt pipe to enumerate downstream devices.
- **Bulk endpoints + URB queue.** Mass-storage and CDC need a generic transfer
  (URB) object with bulk submit/complete and asynchronous completion, replacing
  the synchronous control-only path.
- **Hot-plug / disconnect.** Enumeration is one-shot at boot. Port Status Change
  events from xHCI should drive attach/detach so devices can come and go.
- **Driver match on class/VID/PID tables.** `usb_match_drivers` calls every
  driver's `probe`; a proper `usb_device_id` match table (like the PCI driver
  layer) would scale to many class drivers.
