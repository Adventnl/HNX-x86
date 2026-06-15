# USB HID

The HID stack (`kernel/usb/hid/`) is a boot-protocol keyboard + mouse driver
layered on the USB core. It registers a single `struct usb_driver`, matches
HID-class interfaces, switches them to the boot protocol, configures and arms the
interrupt-IN endpoint, and turns each report into unified input events via the
input-emit layer in `kernel/input/hid/`.

Bring-up in `kernel_main()`:

```c
hid_init();             /* register the "usb-hid" usb_driver        */
usb_match_drivers();    /* probe enumerated devices -> hid_probe()  */
unified_input_init();   /* "[OK] Unified input stack online"        */
hid_tests_run();        /* drive known reports through the handlers  */
```

## Architecture

```
   usb_match_drivers
        |
   hid_probe (iface_class == 0x03)
        |  SET_PROTOCOL=0 (boot), SET_IDLE=0   (class control requests)
        |  configure_endpoint(interrupt-IN), submit_interrupt(report_buf)
        v
   struct hid_device  (type = KEYBOARD | MOUSE)
        |
   hid_poll  --(bus->poll_interrupt)-->  report bytes in report_buf
        |                                     |
        +---- type==KEYBOARD --> hid_keyboard_handle_report (8-byte boot report)
        |                              |  diff vs g_prev[8]
        |                              |  input_emit_key(keycode, down, USB_KEYBOARD)
        |                              |  input_emit_text(hid_usage_to_char, USB_KEYBOARD)
        |
        +---- type==MOUSE -----> hid_mouse_handle_report (buttons, dx, dy, wheel)
                                       input_emit_mouse_move / _wheel (USB_MOUSE)
                                              |
                                              v
                                  kernel/input (input_queue, mouse_event, TTY)
```

The handlers (`hid_keyboard_handle_report`, `hid_mouse_handle_report`) are the
same functions the interrupt-poll path calls and the same the self-tests drive,
so test coverage exercises the production decode path exactly.

### End-to-end data flow

A keypress on a USB keyboard travels: xHCI event ring → `hid_poll` →
`bus->poll_interrupt` returns the 8-byte report in `report_buf` →
`hid_keyboard_handle_report` diffs it against `g_prev` → for each newly-pressed
usage it calls `input_emit_key(keycode, 1, INPUT_SRC_USB_KEYBOARD)` and, for
printables, `input_emit_text(c, INPUT_SRC_USB_KEYBOARD)`. The text emit both
pushes an `INPUT_EVENT_TEXT` onto the shared input queue *and* calls
`tty_input_char(c)` — the exact function the PS/2 keyboard reaches via
`keyboard_emit_char`. From the TTY's perspective the two keyboards are
identical. A mouse report follows the parallel path through
`hid_mouse_handle_report` → `input_emit_mouse_move` → `mouse_process` → the
`mouse_event` ring (see `docs/input.md`).

### `hid_probe` (`hid.c`)

1. Reject the interface if `iface->iface_class != HID_INTERFACE_CLASS` (0x03).
2. Pick the type: `iface->iface_protocol == 2` → `HID_TYPE_MOUSE`, else
   `HID_TYPE_KEYBOARD`.
3. `SET_PROTOCOL` value 0 (boot) and `SET_IDLE` value 0, both via `usb_control`
   with `USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE` and
   `wIndex = iface->number`.
4. Find the interrupt-IN endpoint (`usb_interface_interrupt_in`), allocate a DMA
   page (`pmm_alloc_page`) as `report_buf`, call `dev->bus->configure_endpoint`
   then `dev->bus->submit_interrupt` to arm the first transfer.
5. Call `hid_keyboard_attach` or `hid_mouse_attach`, emit
   `HW_EVENT_USB_DEVICE_ATTACHED`, return 0.

### Device pool and polling (`hid.c`)

HID devices live in a static pool `g_hids[USB_MAX_DEVICES]` (16 entries, reusing
the USB core's device cap). `alloc_hid` returns the first free slot or NULL;
`hid_device_count` and `hid_device_at` iterate the in-use entries.

`hid_poll` walks every active `hid_device`, calls the bus's `poll_interrupt`
callback (`dev->bus->poll_interrupt`) to test whether the interrupt-IN transfer
completed, and on data dispatches the bytes in `report_buf` to
`hid_keyboard_handle_report` or `hid_mouse_handle_report` according to `type`,
then re-arms the endpoint with another `submit_interrupt`. Because no xHCI
interrupt is wired, `hid_poll` is the mechanism that turns the polled event ring
into delivered HID reports. The control requests issued during probe
(`SET_PROTOCOL`, `SET_IDLE`) and the report buffer are the only per-device state;
everything else is recomputed from each report.

### Report-descriptor walk (`hid_report.c`)

Although decode assumes the boot layouts, `hid_report_parse` walks the real HID
report descriptor to classify a device. Each *short item* is a one-byte prefix
(`bTag[7:4] bType[3:2] bSize[1:0]`) followed by 0/1/2/4 data bytes (size 3 means
4 bytes). The parser:

- tracks the current global Report Size and Report Count;
- on a Global `Usage Page` item records the first page and sets `is_keyboard`
  (page 0x07) / `is_mouse` (page 0x09);
- on a Main `Input` item accumulates `input_bits += report_size * report_count`
  and increments `num_inputs`;
- bounds-checks `off + nbytes > len` and stops cleanly on a truncated descriptor.

The result (`struct hid_report_info`) gives a device classification independent of
the interface protocol byte, which is the hook for future report-protocol support.

## File map

| File | Responsibility |
|------|----------------|
| `kernel/usb/hid/hid.h` | `struct hid_device`, type and request constants, core API. |
| `kernel/usb/hid/hid.c` | `usb_driver` registration, `hid_probe`, `hid_poll`, device pool. |
| `kernel/usb/hid/hid_keyboard.c/.h` | 8-byte boot-report decode, key-down/up diff, text emit. |
| `kernel/usb/hid/hid_mouse.c/.h` | boot mouse report decode (buttons/dx/dy/wheel). |
| `kernel/usb/hid/hid_report.c/.h` | report-descriptor item parser, usage-page constants. |
| `kernel/usb/hid/hid_usage.c/.h` | usage→char / usage→keycode mapping, modifier bit masks. |
| `kernel/input/hid/hid_input.c/.h` | `unified_input_init` + `input_emit_*` bridge into `kernel/input`. |
| `kernel/tests/hid_tests.c` | drives a known press/move report through the handlers. |
| `tools/usb/decode_hid_report.py` | host-side report-descriptor decoder (debug aid). |

## Data structures

### `struct hid_device` (`hid.h`)

| Field | Type | Meaning |
|-------|------|---------|
| `usb` | `struct usb_device *` | back-pointer. |
| `type` | `uint8_t` | `HID_TYPE_KEYBOARD` (1) or `HID_TYPE_MOUSE` (2). |
| `intr_ep` | `uint8_t` | interrupt-IN endpoint address. |
| `intr_mps` | `uint16_t` | interrupt endpoint max packet. |
| `interval` | `uint8_t` | polling interval. |
| `report_buf` | `uint64_t` | identity-mapped DMA report buffer. |
| `in_use` | `int` | pool flag. |

### `struct hid_report_info` (`hid_report.h`)

`usage_page` (first global Usage Page), `input_bits` (total Input bits),
`num_inputs` (Input main items), `is_keyboard` / `is_mouse` flags set from the
usage page.

### Boot report layouts

- **Keyboard (8 bytes):** byte 0 = modifier bitmask, byte 1 = reserved, bytes
  2..7 = up to six pressed usage codes (0 = empty). `g_prev[8]` in
  `hid_keyboard.c` holds the previous report for transition detection.
- **Mouse (3–4 bytes):** byte 0 = buttons (bit0 L, bit1 R, bit2 M), byte 1 = dx
  (signed), byte 2 = dy (signed), byte 3 = wheel (signed, parsed only if
  `len >= 4`).

## Key APIs

### Core (`hid.h`)

- `void hid_init(void)` — clears the pool, registers the `"usb-hid"` driver, logs
  `USB HID core online`.
- `void hid_poll(void)` — poll each device's interrupt endpoint; dispatch report
  bytes to keyboard/mouse handler and re-arm.
- `int hid_device_count(void)` / `struct hid_device *hid_device_at(int)`.

### Keyboard (`hid_keyboard.h`)

- `void hid_keyboard_attach(struct usb_device *)` — zero `g_prev`, log
  `USB keyboard online`.
- `void hid_keyboard_handle_report(dev, report, len)` — require `len >= 8`,
  shift = `mods & (HID_MOD_LSHIFT|HID_MOD_RSHIFT)`; for each previous key not in
  the current report emit `INPUT_EVENT_KEY_UP`; for each new key emit
  `INPUT_EVENT_KEY_DOWN` plus a text event from `hid_usage_to_char`; then
  `memcpy(g_prev, report, 8)`.

### Mouse (`hid_mouse.h`)

- `void hid_mouse_attach(struct usb_device *)` — log `USB mouse online`.
- `void hid_mouse_handle_report(dev, report, len)` — require `len >= 3`,
  sign-extend dx/dy/wheel, call `input_emit_mouse_move(dx, dy, buttons,
  INPUT_SRC_USB_MOUSE)` and `input_emit_mouse_wheel` when nonzero.

### Report / usage (`hid_report.h`, `hid_usage.h`)

- `int hid_report_parse(desc, len, *out)`.
- `char hid_usage_to_char(usage, shift)` / `uint16_t hid_usage_to_keycode(usage)`
  (identity).

### Unified bridge (`hid_input.h`)

- `void unified_input_init(void)` — `mouse_init()`, log
  `Unified input stack online`.
- `void input_emit_key(keycode, down, source)` — push `INPUT_EVENT_KEY_DOWN/UP`,
  emit `HW_EVENT_INPUT`.
- `void input_emit_text(c, source)` — push `INPUT_EVENT_TEXT` **and** call
  `tty_input_char(c)` (same TTY path as PS/2).
- `void input_emit_mouse_move/_button/_wheel(...)` — forward to `mouse_process`.

## Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `HID_TYPE_KEYBOARD` / `HID_TYPE_MOUSE` | 1 / 2 | `hid_device.type`. |
| `HID_INTERFACE_CLASS` | 0x03 | USB HID class matched in `hid_probe`. |
| `HID_REQ_GET_REPORT` | 0x01 | class request. |
| `HID_REQ_SET_IDLE` | 0x0A | class request (value 0 = no idle reports). |
| `HID_REQ_SET_PROTOCOL` | 0x0B | class request (value 0 = boot protocol). |
| `HID_USAGE_PAGE_GENERIC` | 0x01 | Generic Desktop. |
| `HID_USAGE_PAGE_KEYBOARD` | 0x07 | Keyboard/Keypad. |
| `HID_USAGE_PAGE_BUTTON` | 0x09 | Button (mouse buttons). |
| `HID_MOD_LCTRL/LSHIFT/LALT/LGUI` | 0x01/0x02/0x04/0x08 | modifier byte bits. |
| `HID_MOD_RCTRL/RSHIFT` | 0x10/0x20 | right-side modifiers. |

Report-descriptor item decode (`hid_report.c`): prefix layout
`bTag[7:4] bType[3:2] bSize[1:0]`; `ITEM_SIZE/TYPE/TAG` macros; types
`TYPE_MAIN`=0, `TYPE_GLOBAL`=1, `TYPE_LOCAL`=2; tags `TAG_INPUT`=0x8,
`TAG_USAGE_PAGE`=0x0, `TAG_REPORT_SIZE`=0x7, `TAG_REPORT_COUNT`=0x9,
`TAG_USAGE`=0x0.

### Usage → ASCII (`hid_usage_to_char`)

- 0x04..0x1D → `a`..`z` (uppercase when shift).
- 0x1E..0x27 → `1`..`9`,`0` (shifted to `!@#$%^&*()`).
- 0x28 `\n` (Enter), 0x2A `\b` (Backspace), 0x2B `\t` (Tab), 0x2C space,
  0x2D..0x38 punctuation with shifted variants; everything else → 0.

## Invariants

- **Boot protocol only.** Devices are forced into boot protocol; the full report
  descriptor is parsed by `hid_report.c` for classification but the fixed 8-byte
  (keyboard) / 3–4-byte (mouse) layouts are assumed for decode.
- **Stateless except `g_prev`.** Key transitions are derived purely by diffing the
  current report against `g_prev[8]`; the handler is idempotent given identical
  reports.
- **Single text path.** `input_emit_text` always routes characters through
  `tty_input_char`, so USB and PS/2 keystrokes are indistinguishable to the TTY.
- **Identity-mapped report buffer.** `report_buf` is a raw PMM page handed
  straight to the HCD as a DMA target.
- **Validated lengths.** `hid_keyboard_handle_report` ignores reports shorter than
  8 bytes; `hid_mouse_handle_report` ignores reports shorter than 3.

## Notes on decode

- **Modifier handling is shift-only.** `hid_keyboard_handle_report` extracts the
  modifier byte and derives `shift = mods & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)` to
  pick the shifted character. Ctrl/Alt/GUI are visible in the modifier byte but
  do not yet alter the emitted character; right-Alt (AltGr) and right-GUI are not
  decoded.
- **Six-key rollover.** The boot report carries at most six simultaneous keycodes
  (bytes 2..7). Holding more keys at once is not representable until
  report-protocol parsing lands.
- **Mouse is relative.** dx/dy/wheel are signed deltas sign-extended from the
  report bytes; there is no absolute coordinate or acceleration — the unified
  mouse layer accumulates the deltas in `mouse_state`.

## Failure modes

- `hid_probe` returns -1 (no device attached) when: the interface is not HID
  class, no interrupt-IN endpoint exists, or the HID device pool / DMA page
  allocation fails. Failure is silent at the USB layer — the device simply stays
  unclaimed.
- The report handlers return early (no-op) on NULL or too-short reports; they
  never fault on malformed input.
- `hid_report_parse` returns -1 on NULL arguments and stops cleanly on a
  truncated descriptor (no over-read).
- No panics anywhere in the HID stack.

## Verification

```
make verify-hid
```

asserts:

```
[OK] USB HID core online
[OK] USB keyboard online
[OK] USB mouse online
[PASS] hid keyboard test
[PASS] hid mouse test
```

`hid_tests_run` (`kernel/tests/hid_tests.c`) drains the input + mouse queues,
then drives a press of usage `0x04` (`{0,0,0x04,0,...}`) through
`hid_keyboard_handle_report` and asserts both an `INPUT_EVENT_KEY_DOWN` with
`code == 0x04` and an `INPUT_EVENT_TEXT` with `code == 'a'`, both tagged
`INPUT_SRC_USB_KEYBOARD`; then a mouse report `{0x01, 5, -5, 0}` and asserts a
`mouse_event` with `source == INPUT_SRC_USB_MOUSE`, `dx == 5`, `dy == -5`. The
USB-keyboard and USB-mouse paths are also re-checked end-to-end by
`make verify-input-unified` (`[PASS] usb keyboard works`, `[PASS] usb mouse
works`). The `make verify-hw-userland` target checks the userland
`hidinfo`/`lsusb` programs. `make verify-prompt6` runs all of these.

## Future expansion

- **Report-protocol devices.** Parse the real report descriptor (the parser
  exists) to support non-boot keyboards/mice and arbitrary field layouts instead
  of the fixed boot reports.
- **Consumer / system controls, multimedia keys, N-key rollover.** The boot
  keyboard caps at six simultaneous keys; report-protocol parsing lifts that.
- **LED / output reports.** `SET_REPORT` (the request constant is not yet
  defined) would drive Caps/Num/Scroll-Lock LEDs.
- **Keymap layers + modifiers.** `hid_usage_to_char` is a flat US-ASCII map; a
  proper keymap (shared with `keymap_us`) plus Ctrl/Alt handling and layouts is
  the next step.
- **Generic HID class (gamepads, touch, tablets).** Field-by-field decode driven
  by `hid_report_info` would let arbitrary HID devices emit input events.
- **Interrupt-driven polling.** `hid_poll` is called from the boot/poll path;
  with xHCI interrupts wired, reports would be delivered by the event-ring ISR.
