# xHCI Host Controller

The xHCI driver brings up the first Extensible Host Controller found on the PCI
bus, establishes the command/event/transfer rings, resets and addresses the
devices hanging off the root hub, and exposes a bus interface (`struct usb_bus`)
that the USB core drives for descriptor enumeration and interrupt polling. It is
a poll-mode driver: no MSI/MSI-X interrupt is wired for completions; every ring
is drained by spinning on the producer cycle bit.

All source lives under `kernel/usb/xhci/`. It is brought up from
`kernel_main()` (see `kernel/src/kernel.c`) after MSI and the driver-lifecycle /
hardware-event-bus layers, by the sequence:

```c
xhci_init();          /* discover, reset, start, scan root hub      */
xhci_tests_run();     /* No-Op round-trip self-test                 */
usb_core_init();
usb_hub_init();
xhci_attach_usb();    /* register the bus + enumerate every port    */
```

## Architecture

```
            PCI (class 0x0C, subclass 0x03, prog-if 0x30)
                            |
                  xhci_init / xhci_controller_setup
                            |
        +-------------------+---------------------------+
        |                   |                           |
   capability regs     operational regs            runtime regs
   (CAPLENGTH..)       (USBCMD/USBSTS/CRCR/         (interrupter 0:
                        DCBAAP/CONFIG/PORTSC)        IMAN/ERSTSZ/
                            |                         ERSTBA/ERDP)
                            |
        +---------+---------+----------+-----------+
        |         |                    |           |
    DCBAA     command ring        event ring   doorbell array
   (slots)   (TRB ring, 256)     (TRB+ERST)    db[0]=cmd, db[slot]=ep
        |
   per-slot device context + EP0 ring + interrupt ring
        |
        v
   xhci_attach_usb  -> struct usb_bus { control, configure_endpoint,
                                        submit_interrupt, poll_interrupt }
        |
        v
   USB core (usb_enumerate_device over EP0 control transfers)
```

The driver supports a single controller (`static struct xhci g_xhci` in
`xhci.c`). Each connected root-hub port is reset, given a slot (Enable Slot),
and addressed (Address Device) before the USB core fetches descriptors.

### Bring-up flow (`xhci_controller_setup`, `xhci_controller.c`)

1. `map_and_parse` — read BAR0 from the PCI device, enable the device, map MMIO
   (2 MiB page), read `CAPLENGTH`/`HCIVERSION`, parse `HCSPARAMS1`
   (`max_slots`, `max_intrs`, `max_ports`), `HCSPARAMS2` (`max_scratchpad`),
   `HCCPARAMS1` (`ac64`, `context_size`). Derive `op = mmio + caplen`,
   `rt = mmio + RTSOFF`, `db = mmio + DBOFF`. Logs `xHCI MMIO mapped` and
   `xHCI capability registers parsed`.
2. `reset_controller` — clear `XHCI_CMD_RUN`, wait for `XHCI_STS_HCH` (halted),
   set `XHCI_CMD_HCRST`, wait for HCRST to clear and `XHCI_STS_CNR` (Controller
   Not Ready) to clear.
3. Write `CONFIG = max_slots` (number of enabled device slots).
4. `xhci_context_init` — allocate the DCBAA page, set up the scratchpad array if
   `max_scratchpad > 0`, program `DCBAAP`.
5. Allocate the identity-mapped DMA bounce page (`bounce_phys`).
6. `setup_event_ring` — allocate the event ring + ERST, write `ERSTSZ`, `ERDP`,
   `ERSTBA`, `IMAN`. Logs `xHCI event ring online`.
7. Allocate the command ring, write `CRCR = (cmd_ring_phys | 1)` (RCS=1). Logs
   `xHCI command ring online`.
8. Set `XHCI_CMD_RUN`, wait for `XHCI_STS_HCH` to clear. Logs
   `xHCI controller started`.

## File map

| File | Responsibility |
|------|----------------|
| `kernel/usb/xhci/xhci.h` | `struct xhci` definition, `XHCI_MAX_SLOTS_CAP`, top-level API. |
| `kernel/usb/xhci/xhci.c` | Discovery (`xhci_init`), root-hub scan, `xhci_attach_usb`, the four `usb_bus` callbacks. |
| `kernel/usb/xhci/xhci_regs.h` | Register offsets and bit masks, `struct xhci_trb`, `struct xhci_erst_entry`, TRB type / completion-code constants. |
| `kernel/usb/xhci/xhci_controller.c/.h` | `xhci_controller_setup`: MMIO map, capability parse, reset, ring/context alloc, run. |
| `kernel/usb/xhci/xhci_ring.c/.h` | TRB ring allocation and `xhci_ring_enqueue` (cycle-bit + Link-TRB wrap). |
| `kernel/usb/xhci/xhci_command.c/.h` | Command submission + the typed commands (No-Op, Enable Slot, Address Device, Configure Endpoint). |
| `kernel/usb/xhci/xhci_event.c/.h` | Event-ring consumer: `xhci_event_poll`, `xhci_event_try`, `xhci_event_drain`. |
| `kernel/usb/xhci/xhci_context.c/.h` | DCBAA + scratchpad allocation, context-block helpers. |
| `kernel/usb/xhci/xhci_port.c/.h` | Port register access and `xhci_port_reset`. |
| `kernel/usb/xhci/xhci_roothub.c/.h` | `xhci_address_port_device` (reset + Enable Slot + Address Device), `xhci_scan_root_hub`, EP0 max-packet by speed. |
| `kernel/usb/xhci/xhci_transfer.c/.h` | `xhci_control_transfer` (3-stage setup/data/status) and `xhci_interrupt_queue`. |
| `kernel/tests/xhci_tests.c` | `xhci_tests_run` self-test (`[PASS] xhci controller test`). |

## Data structures

### `struct xhci` (`xhci.h`)

The single controller instance. `XHCI_MAX_SLOTS_CAP` is 64.

| Field | Type | Meaning |
|-------|------|---------|
| `pci` | `struct pci_device *` | The matched PCI function. |
| `mmio` / `op` / `rt` | `volatile uint8_t *` | BAR0 base; operational regs (`mmio+caplen`); runtime regs (`mmio+RTSOFF`). |
| `db` | `volatile uint32_t *` | Doorbell array (`mmio+DBOFF`). |
| `caplen`, `version` | `uint8_t`, `uint16_t` | `CAPLENGTH`, `HCIVERSION`. |
| `max_slots`, `max_ports`, `max_intrs` | `uint8_t`/`uint16_t` | From `HCSPARAMS1`. |
| `context_size` | `uint8_t` | 32 or 64 bytes (from `HCCPARAMS1.CSZ`). |
| `ac64` | `uint8_t` | 64-bit addressing capable. |
| `max_scratchpad` | `uint32_t` | Scratchpad buffers the HC requires. |
| `dcbaa` | `uint64_t *` | Device Context Base Address Array. |
| `cmd_ring`, `cmd_enqueue`, `cmd_cycle` | ring + cursor + producer cycle | Command ring state. |
| `event_ring`, `erst`, `event_dequeue`, `event_cycle` | ring + ERST + cursor + consumer cycle | Event ring state. |
| `scratchpad_array`, `bounce_phys` | `uint64_t` | Scratchpad pointer array phys; identity-mapped DMA bounce page. |
| `dev_context[64]` | `void *` | Per-slot device context page. |
| `ep0_ring[64]`, `ep0_cycle[64]`, `ep0_enqueue[64]` | per-slot EP0 ring state. |
| `intr_ring[64]`, `intr_cycle[64]`, `intr_enqueue[64]`, `intr_dci[64]` | per-slot interrupt-IN endpoint ring + its DCI. |
| `initialized`, `ports_connected` | `int` | Set after `RUN`; root-hub connect count. |

### `struct xhci_trb` (`xhci_regs.h`)

The universal 16-byte Transfer Request Block, packed:

```c
struct xhci_trb { uint64_t parameter; uint32_t status; uint32_t control; };
```

`control` carries the TRB type in bits [15:10] (`XHCI_TRB_TYPE(t)`,
`XHCI_TRB_GET_TYPE(c)`) and the cycle bit in bit 0 (`XHCI_TRB_CYCLE`).

### `struct xhci_erst_entry` (`xhci_regs.h`)

Event Ring Segment Table entry, packed:

```c
struct xhci_erst_entry { uint64_t ring_base; uint32_t ring_size; uint32_t reserved; };
```

### Context blocks (`xhci_context.c`, used in `xhci_roothub.c`/`xhci.c`)

A context block is one page. The Input Context is `[Input Control][Slot][EP0]
[EP1]...`; the Device Context is `[Slot][EP0]...`. Each sub-context is
`context_size` (32 or 64) bytes; helper `xhci_ctx_dword` returns the dword array
for a given index. Fields written during Address Device:

- Input Control Context: dword 0 = drop flags, dword 1 = add flags
  (`0x3` = add slot A0 + EP0 A1).
- Slot Context: dword 0 = `(speed << 20) | (1 << 27)` (speed + context entries),
  dword 1 = `(port << 16)` (root-hub port number).
- EP0 Context: dword 1 = `(3 << 1) | (4 << 3) | (mps << 16)` (CErr=3, type 4 =
  Control, max packet), dword 2/3 = TR dequeue pointer `| DCS=1`, dword 4 = 8
  (average TRB length).

## Register map (`xhci_regs.h`)

### Capability registers (from BAR0)

| Macro | Offset | Notes |
|-------|--------|-------|
| `XHCI_CAP_CAPLENGTH` | 0x00 | u8; operational regs at `mmio+CAPLENGTH`. |
| `XHCI_CAP_HCIVERSION` | 0x02 | u16. |
| `XHCI_CAP_HCSPARAMS1` | 0x04 | `XHCI_HCS1_MAXSLOTS/MAXINTRS/MAXPORTS`. |
| `XHCI_CAP_HCSPARAMS2` | 0x08 | `XHCI_HCS2_MAXSCRATCH`. |
| `XHCI_CAP_HCSPARAMS3` | 0x0C | |
| `XHCI_CAP_HCCPARAMS1` | 0x10 | `XHCI_HCC1_AC64/CSZ/XECP`. |
| `XHCI_CAP_DBOFF` | 0x14 | doorbell array offset. |
| `XHCI_CAP_RTSOFF` | 0x18 | runtime-register offset. |
| `XHCI_CAP_HCCPARAMS2` | 0x1C | |

### Operational registers (from `mmio + CAPLENGTH`)

| Macro | Offset | Bits |
|-------|--------|------|
| `XHCI_OP_USBCMD` | 0x00 | `XHCI_CMD_RUN` (b0), `XHCI_CMD_HCRST` (b1), `XHCI_CMD_INTE` (b2), `XHCI_CMD_HSEE` (b3). |
| `XHCI_OP_USBSTS` | 0x04 | `XHCI_STS_HCH` (b0), `XHCI_STS_HSE` (b2), `XHCI_STS_EINT` (b3), `XHCI_STS_PCD` (b4), `XHCI_STS_CNR` (b11). |
| `XHCI_OP_PAGESIZE` | 0x08 | |
| `XHCI_OP_DNCTRL` | 0x14 | |
| `XHCI_OP_CRCR` | 0x18 | u64; command-ring base `| RCS`. |
| `XHCI_OP_DCBAAP` | 0x30 | u64; DCBAA base. |
| `XHCI_OP_CONFIG` | 0x38 | low byte = number of enabled device slots. |
| `XHCI_OP_PORTS` | 0x400 | first PORTSC; `XHCI_PORT_STRIDE` = 0x10 apart, `XHCI_PORT_PORTSC` = 0x00. |

`PORTSC` bits: `XHCI_PORTSC_CCS` (b0, connect), `XHCI_PORTSC_PED` (b1, enabled),
`XHCI_PORTSC_OCA` (b3), `XHCI_PORTSC_PR` (b4, reset), `XHCI_PORTSC_PP` (b9),
`XHCI_PORTSC_SPEED(x)` (bits [13:10]), `XHCI_PORTSC_CSC` (b17),
`XHCI_PORTSC_PEC` (b18), `XHCI_PORTSC_PRC` (b21), and the write-1-to-clear mask
`XHCI_PORTSC_RW1CS`. Speed IDs: `XHCI_SPEED_FULL`=1, `XHCI_SPEED_LOW`=2,
`XHCI_SPEED_HIGH`=3, `XHCI_SPEED_SUPER`=4.

### Runtime / interrupter registers (from `mmio + RTSOFF`)

Interrupter 0 lives at `XHCI_RT_IR0` (0x20). Within it:
`XHCI_IR_IMAN` (0x00, bits `XHCI_IMAN_IP`/`XHCI_IMAN_IE`), `XHCI_IR_IMOD`
(0x04), `XHCI_IR_ERSTSZ` (0x08), `XHCI_IR_ERSTBA` (0x10, u64),
`XHCI_IR_ERDP` (0x18, u64, bit `XHCI_ERDP_EHB` = Event Handler Busy, RW1C).

### Doorbells (from `mmio + DBOFF`)

`db[0]` is the command doorbell (write 0). `db[slot]` is the endpoint doorbell;
the written value is the DCI (`2*ep + dir_in`; EP0 control = 1).

### TRB types and completion codes (`xhci_regs.h`)

Types: `XHCI_TRB_NORMAL`=1, `_SETUP`=2, `_DATA`=3, `_STATUS`=4, `_LINK`=6,
`_ENABLE_SLOT`=9, `_DISABLE_SLOT`=10, `_ADDRESS_DEVICE`=11, `_CONFIG_EP`=12,
`_EVAL_CONTEXT`=13, `_NOOP_CMD`=23, `_TRANSFER_EVENT`=32, `_CMD_COMPLETION`=33,
`_PORT_STATUS`=34. Control helper bits: `XHCI_TRB_IOC` (b5), `XHCI_TRB_IDT`
(b6), `XHCI_TRB_CHAIN` (b4), `XHCI_TRB_ENT` (b1), `XHCI_TRB_TOGGLE` (Link b1),
`XHCI_TRB_DIR_IN` (b16). Transfer types: `XHCI_TRT_NO_DATA`=0, `XHCI_TRT_OUT`=2,
`XHCI_TRT_IN`=3. Completion: `XHCI_CC(status)` = `(status >> 24) & 0xFF`,
`XHCI_CC_SUCCESS`=1. Event helpers: `XHCI_EVENT_SLOT(control)` =
`(control >> 24) & 0xFF`.

## Key APIs

### Top level (`xhci.h`)

- `void xhci_init(void)` — discover the first xHCI on PCI (`pci_find_by_class_prog`),
  run `xhci_controller_setup`, validate the ring path with a No-Op, scan the root hub.
- `struct xhci *xhci_controller(void)` — the single instance, or NULL.
- `void xhci_scan_root_hub(struct xhci *)` — count and log connected ports.
- `void xhci_attach_usb(void)` — register the `usb_bus`, register the root hub
  with the USB core, address every connected port, allocate a `usb_device` per
  port, and call `usb_enumerate_device`.

### Rings & commands

- `struct xhci_trb *xhci_ring_alloc(void)` — one page initialized with a Link TRB
  at index 255 (`XHCI_RING_TRBS - 1`).
- `uint64_t xhci_ring_enqueue(ring, *enqueue, *cycle, parameter, status, control)`
  — write a TRB with the current cycle, advance, and wrap at the Link TRB
  (toggling the Link cycle and `*cycle`).
- `int xhci_command_submit(xhc, parameter, control, *slot_out)` — enqueue a
  command TRB, ring `db[0]`, poll for a Command Completion event.
- `int xhci_cmd_noop / _enable_slot(*slot) / _address_device(slot, inctx_phys) /
  _configure_endpoint(slot, inctx_phys)`.

### Events

- `int xhci_event_poll(xhc, want_type, *out)` — spin up to `XHCI_EVENT_SPIN`
  (2,000,000) for an event of `want_type` (0 = any). Returns 1 if found.
- `int xhci_event_try(xhc, *out)` — single non-blocking consume.
- `void xhci_event_drain(xhc)` — discard all pending events.

### Ports / addressing

- `uint32_t xhci_port_read(xhc, port)` / `int xhci_port_connected(...)` /
  `uint8_t xhci_port_speed(...)` / `const char *xhci_speed_name(speed)`.
- `int xhci_port_reset(xhc, port)` — assert `PR`, spin to `PRC`/`PED`, ack `PRC`.
- `int xhci_address_port_device(xhc, port, *speed_out)` — reset + Enable Slot +
  build input context + Address Device; returns the slot (0 on failure).
- `uint16_t xhci_ep0_max_packet(speed)` — 512 (super), 64 (high), 8 (low/full).

### Transfers

- `int xhci_control_transfer(xhc, slot, bmRequestType, bRequest, wValue, wIndex,
  wLength, data)` — Setup/Data/Status TRBs on the EP0 ring, ring `db[slot]=1`,
  poll for a Transfer Event, copy IN data back from the bounce page.
- `int xhci_interrupt_queue(xhc, slot, dma_buf, length)` — Normal TRB on the
  interrupt-IN ring with `XHCI_TRB_IOC`, ring `db[slot]=dci`.

### Bus callbacks (`xhci.c`, installed by `xhci_attach_usb`)

`xhci_bus_control`, `xhci_bus_configure_endpoint`, `xhci_bus_submit_interrupt`,
`xhci_bus_poll_interrupt` — wired into `struct usb_bus` function pointers so the
USB core never touches xHCI registers directly.

## Invariants

- **Single controller.** Only the first matching PCI function is used; `g_present`
  gates `xhci_controller()`.
- **Poll mode only.** `IMAN.IE` is left clear; completions are found by spinning
  on the cycle bit and `XHCI_*_SPIN` budgets. No xHCI interrupt vector is routed.
- **One page per ring, 256 TRBs.** `XHCI_RING_TRBS` = 256; the last entry is a
  Link TRB, so 255 are usable. The Link TRB's `XHCI_TRB_TOGGLE` flips the
  producer cycle on wrap.
- **Cycle-bit discipline.** Producers (command/EP0/interrupt rings) start at
  cycle 1; the consumer (event ring) starts at cycle 1 and toggles on wrap.
  A TRB whose cycle ≠ the consumer cycle has not yet been produced.
- **DCBAA[0] is reserved.** When `max_scratchpad > 0`, DCBAA[0] points at the
  scratchpad pointer array; device contexts occupy slots 1..`max_slots`.
- **EP0 DCI is 1.** Control transfers ring `db[slot] = 1`. Interrupt endpoints
  use `intr_dci[slot]` (`2*ep + 1` for IN).
- **Identity-mapped DMA.** Rings, contexts and the bounce page are PMM pages used
  with virt==phys; control-transfer payloads are staged through `bounce_phys`.
- **CONFIG before contexts run.** `CONFIG` (enabled slot count) is written before
  the controller is set to `RUN`.

## Failure modes

`int`-returning functions use 0 = success / negative = failure; command helpers
return the completion code (`XHCI_CC_SUCCESS` = 1). Specific paths:

- `xhci_init`: no PCI match → `kernel_log_warn("xHCI controller not found")`,
  returns (no controller registered).
- `xhci_controller_setup` errors (all `kernel_log_error`, return -1): BAR0 not
  MMIO (`xHCI: BAR0 is not MMIO`), MMIO map failure (`xHCI: MMIO map failed`),
  reset timeout (`xHCI: reset (HCRST) timed out`), CNR timeout
  (`xHCI: controller-not-ready timed out`), context init (`xHCI: context init
  failed`), event ring alloc (`xHCI: event ring alloc failed`), run timeout
  (`xHCI: controller did not start`). On any of these `xhci_init` logs
  `xHCI: controller setup failed` and leaves `g_present` clear.
- No-Op after setup: if it does not return success, `xHCI noop command did not
  complete` (warn) — boot continues.
- `xhci_port_reset`: timeout or no `PED` → returns -1.
- `xhci_address_port_device`: Enable Slot or Address Device not successful, or a
  page alloc failing (`PMM_INVALID_PAGE`) → returns 0.
- `xhci_control_transfer` / `xhci_interrupt_queue`: timeout or bad completion
  code → negative.
- The driver **does not panic**; a missing or broken controller degrades to "no
  USB devices" and the corresponding verify markers simply never appear.

## Verification

Marker-grep model (`tools/verify_qemu.py`): boot the image headless in QEMU +
OVMF and assert every `--expect` substring appears on COM1 within the timeout.
QEMU is launched with a `qemu-xhci` controller plus a `usb-kbd` and `usb-mouse`
on `xhci.0` (see `tools/run_qemu.py` / `tools/verify_qemu.py`), so there are real
devices on the root hub.

```
make verify-xhci
```

asserts:

```
[OK] xHCI controller found
[OK] xHCI MMIO mapped
[OK] xHCI command ring online
[OK] xHCI event ring online
[OK] xHCI controller started
[OK] xHCI root hub scanned
```

The `xhci_tests_run` self-test additionally emits `[PASS] xhci controller test`
after confirming `initialized`, `max_ports != 0`, and a second No-Op round-trip
(`xhci_cmd_noop == XHCI_CC_SUCCESS`); this marker is checked by
`make verify-xhci-expanded` (see `Makefile.production`). Other relevant log lines
emitted during bring-up: `xHCI controller found`, `xHCI MMIO mapped`,
`xHCI capability registers parsed`, `xHCI command ring online`,
`xHCI event ring online`, `xHCI controller started`,
`xHCI command ring verified (noop)`, per-port `xhci port up:` / speed name lines,
`xhci ports up  :`, `xHCI root hub scanned`.

Run the matrix under multiple memory sizes with `make verify-qemu-matrix`
(128M/256M/512M/1024M/2048M). The full `make verify-prompt6` chain runs
`verify-xhci` along with the USB/HID/input verifications.

## Future expansion

- **Interrupt-driven completions.** Set `IMAN.IE`, route the controller's MSI/MSI-X
  vector (the `kernel/msi` layer already parses capabilities), and replace the
  `XHCI_*_SPIN` busy-loops with an event-ring ISR + waitqueue wakeups.
- **Multiple controllers / multi-segment ERST.** `g_xhci` is a singleton and the
  ERST has a single 256-TRB segment; generalize to a list of controllers and a
  multi-segment event ring for large event bursts.
- **External hubs.** `xhci_attach_usb` only enumerates root-hub ports. Driving a
  USB hub class device requires Configure Endpoint for the hub's status pipe plus
  per-port reset routed through the hub (the `usb_hub` foundation already models
  ports).
- **Bulk / isochronous / scatter-gather TRBs.** Only Control and Interrupt-IN
  transfers exist; bulk (mass storage) and isochronous (audio/video) need Normal
  TRB chains, ring TD fragmentation, and proper short-packet handling instead of
  the single bounce page.
- **Bandwidth and stream support.** Configure Endpoint currently configures one
  endpoint at a time with fixed `CErr`/interval encodings; a full implementation
  needs bandwidth checking, streams (SS bulk), and Evaluate Context for MPS
  updates after the 8-byte descriptor probe.
- **64-byte context handling.** `context_size` is tracked but several code paths
  assume the 32-byte layout; honoring `ac64`/`CSZ` everywhere is required for
  controllers that demand 64-byte contexts.
