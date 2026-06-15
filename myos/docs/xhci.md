# xHCI Host Controller

MyOS drives USB through a from-scratch **xHCI 1.x** host-controller driver
(`kernel/usb/xhci/`). No Linux/BSD code is used. The controller is driven in a
**polled** model: commands and transfers are posted to TRB rings and the event
ring is polled for completion. This keeps enumeration deterministic inside the
verification window and avoids wiring an MSI handler before it is needed.

## Files

| File | Responsibility |
|------|----------------|
| `xhci_regs.h` | Register offsets, bitfields, TRB + context layouts |
| `xhci.c` | Discovery, top-level bring-up, USB-core bus glue |
| `xhci_controller.c` | MMIO map, capability parse, reset, rings, start |
| `xhci_ring.c` | TRB ring producer (Link-TRB wrap + cycle toggle) |
| `xhci_event.c` | Event-ring consumer (poll / try / drain) |
| `xhci_command.c` | Command ring: No-Op, Enable Slot, Address Device, Configure Endpoint |
| `xhci_context.c` | DCBAA, scratchpad and device/input context allocation |
| `xhci_port.c` | Root-hub port registers, reset, speed detection |
| `xhci_transfer.c` | EP0 control transfers + interrupt-IN queuing |
| `xhci_roothub.c` | Port scan + per-port device addressing |

## Register banks

BAR0 exposes four banks. After mapping BAR0 with `vmm_map_mmio_2m()`:

```
capability   @ base
operational  @ base + CAPLENGTH
runtime      @ base + RTSOFF
doorbell     @ base + DBOFF
```

`CAPLENGTH`/`HCIVERSION` share the first 32-bit register and are read with a
single 32-bit access (some controllers reject sub-dword reads here).

## Memory structures (DMA)

All controller-visible structures are `pmm_alloc_page()` buffers, where physical
== identity-mapped virtual, so the returned address is used both as a kernel
pointer and as the physical address programmed into hardware.

- **DCBAA** — Device Context Base Address Array, indexed by slot id. Entry 0 is
  the scratchpad-buffer-array pointer when the controller requests scratchpad.
- **Command ring** — 256-TRB page, last TRB is a Link TRB back to the base.
- **Event ring + ERST** — a 256-TRB event ring plus a one-entry Event Ring
  Segment Table; `ERSTSZ`/`ERDP`/`ERSTBA` are programmed for interrupter 0.
- **Device / input contexts** — one page each; 32- or 64-byte contexts depending
  on the HCCPARAMS1 CSZ bit.
- **DMA bounce page** — control-transfer payloads pass through one bounce page so
  callers may use ordinary (heap) buffers.

## Bring-up sequence (`xhci_controller_setup`)

1. Map BAR0, enable PCI memory + bus-master, parse capability registers.
2. Stop + `HCRST` reset; wait for `CNR` (controller-not-ready) to clear.
3. Program `CONFIG` with the max enabled slots.
4. Allocate DCBAA + scratchpad (`xhci_context_init`).
5. Allocate the command ring; program `CRCR` with RCS=1.
6. Allocate the event ring + ERST; program interrupter 0.
7. Set `USBCMD.RUN`; wait for `HCH` to clear.

A **No-Op command** is then issued and its Command Completion Event polled off
the event ring — proving the command ring, event ring, doorbell and cycle-state
logic before any device is touched.

## Device addressing (`xhci_address_port_device`)

Per connected root port: reset the port, **Enable Slot**, allocate a device
context + EP0 transfer ring, build an input context (slot context with speed +
root-hub-port number, EP0 control endpoint with the speed-appropriate max packet
size), then **Address Device**. The slot is left ready for EP0 control transfers,
which the USB core uses to fetch descriptors.

## Markers

```
[OK] xHCI controller found
[OK] xHCI MMIO mapped
[OK] xHCI capability registers parsed
[OK] xHCI command ring online
[OK] xHCI event ring online
[OK] xHCI controller started
[OK] xHCI root hub scanned
```

`make verify-xhci` checks these against QEMU `qemu-xhci`.

## QEMU

`tools/run_qemu.py` / `tools/verify_qemu.py` attach:

```
-device qemu-xhci,id=xhci
-device usb-kbd,bus=xhci.0
-device usb-mouse,bus=xhci.0
```

On `qemu-xhci`, `usb-kbd`/`usb-mouse` attach as high-speed devices on root ports
5 and 6 (USB2 ports); the driver reports them via `[PASS] usb enumeration`.

## Known limitations / future work

- Polled event ring (no MSI handler yet — the MSI/MSI-X foundation exists, see
  [msi.md](msi.md)).
- EP0 max packet size is set from link speed; a full-speed device reporting a
  non-default `bMaxPacketSize0` would need an Evaluate Context (not required for
  the QEMU HID devices).
- External hubs are modelled but not yet enumerated past the root hub.
- USB mass storage is intentionally deferred to a later phase.
