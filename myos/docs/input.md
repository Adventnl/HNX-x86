# Input Stack (PS/2 keyboard)

## Path

```
IRQ1 (8042) ─► ps2_irq_handler ─► ps2_keyboard_handle_scancode
                                      ├─► input_queue (raw key events)
                                      └─► keymap_us_translate ─► keyboard_emit_char
                                                                   └─► tty_input_char (canonical)
```

* **8042 controller** (`ps2.c`): flush, enable port 1, set the config byte to
  generate IRQ1, install the IRQ handler on vector 0x21, and route ISA IRQ1 →
  0x21 through the **I/O APIC** (`ioapic.c`) — necessary because the legacy PIC
  is masked and the LAPIC is the interrupt controller.
* **PS/2 keyboard** (`ps2_keyboard.c`): scancode-set-1 decode with shift state;
  pushes `struct input_event` to the queue and emits decoded characters.
* **keymap_us** (`keymap_us.c`): normal + shifted ASCII tables; `\n` for Enter,
  `\b` for Backspace.
* **input_queue** (`input_queue.c`): IRQ-safe ring of input events.
* **ps2_mouse.c**: foundation stub (second port recognized; driven in Prompt 6).

## Scripted injection

For headless verification there is no physical keyboard, so
`keyboard_inject_scancode()` feeds the exact same decode pipeline. The kernel
self-test injects `a b c` and confirms they decode + queue:
`[PASS] keyboard scripted injection`.

Markers: `[OK] PS/2 controller online`, `[OK] Keyboard input online`.

See [tty.md](tty.md) for the canonical line discipline that consumes keyboard
characters.

## Prompt 6 — unified input (PS/2 + USB)

Prompt 6 unifies PS/2 and USB-HID input under one event model (`kernel/input/input_event.h`): `INPUT_EVENT_KEY_DOWN/KEY_UP/TEXT/MOUSE_MOVE/MOUSE_BUTTON/MOUSE_WHEEL`, each tagged with an `input_source` (PS/2 keyboard, USB keyboard, USB mouse). The bridge `kernel/input/hid/hid_input.c` provides the sinks `input_emit_key/text/mouse_*`: key events go to the input queue, text goes to the TTY line discipline, pointer events go to the mouse queue (`kernel/input/mouse/`). The PS/2 path keeps working and now tags its events with `INPUT_SRC_PS2_KEYBOARD`; the USB HID boot keyboard/mouse feed the same sinks. Userland reads input via `SYS_INPUT_POLL` (keytest/inputtest) and mouse via `SYS_MOUSE_POLL` (mousetest). Markers: `[OK] Unified input stack online`, `[PASS] ps2 keyboard still works`, `[PASS] usb keyboard works`, `[PASS] usb mouse works`, `[PASS] tty accepts unified keyboard input`. See [hid.md](hid.md) and [hardware_compatibility.md](hardware_compatibility.md).
