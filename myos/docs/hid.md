# USB HID

The HID stack (`kernel/usb/hid/`) is a boot-protocol keyboard + mouse driver
layered on the USB core. It matches HID-class interfaces, switches them to the
boot protocol, configures the interrupt-IN endpoint, and turns reports into
unified input events.

## Files

| File | Responsibility |
|------|----------------|
| `hid.c` | HID core: probe/bind, boot-protocol setup, endpoint config, poll |
| `hid_report.c` | Minimal report-descriptor walker (usage page + report size) |
| `hid_keyboard.c` | Boot keyboard report → key + text events |
| `hid_mouse.c` | Boot mouse report → pointer events |
| `hid_usage.c` | Keyboard usage → ASCII (with shift) |

## Binding (`hid_probe`)

A device is claimed when its first interface has class `0x03` (HID) and an
interrupt-IN endpoint. The driver then:

1. `SET_PROTOCOL(boot)` and `SET_IDLE(0)` (class requests over EP0).
2. `configure_endpoint()` — a Configure Endpoint xHCI command builds the
   interrupt-IN endpoint context (EP type 7, speed-derived interval).
3. Allocates a DMA report buffer and arms one interrupt transfer.
4. Attaches `hid_keyboard` (protocol 1) or `hid_mouse` (protocol 2).

Markers: `[OK] USB HID core online`, `[OK] USB keyboard online`,
`[OK] USB mouse online`.

## Boot reports

**Keyboard** (8 bytes): `[modifiers][reserved][keycode×6]`. `hid_keyboard_handle_report`
diffs against the previous report to find newly pressed/released usages, emits
`INPUT_EVENT_KEY_DOWN/UP`, and for printable keys emits `INPUT_EVENT_TEXT` (also
fed to the TTY).

**Mouse** (3–4 bytes): `[buttons][dx][dy]([wheel])`, `dx`/`dy` signed. Movement,
button and wheel changes become unified mouse events.

## Verification

The handlers are the exact functions the interrupt path calls, so the self-tests
feed known boot reports through them and assert the unified output — the same
technique the PS/2 layer uses for scancode injection:

```
[PASS] hid keyboard test
[PASS] hid mouse test
```

`make verify-hid` checks the online markers plus these.

## Report descriptor parser

`hid_report.c` walks short HID items (Main/Global/Local) to extract the primary
usage page and the total input-report size. MyOS drives boot-protocol devices
(fixed layout) so this is used to *validate* the descriptor rather than build a
full field map. The Python companion `tools/usb/decode_hid_report.py` decodes a
report descriptor offline.

## Live input

When real keypresses/movements occur, `hid_poll()` consumes the interrupt
transfer's completion event, dispatches the report, and re-arms the transfer. In
headless verification no physical input is generated, so correctness is proven
by the report-injection self-tests above.
