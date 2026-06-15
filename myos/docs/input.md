# Unified Input Stack

The input stack (`kernel/input/`, `kernel/tty/`) unifies PS/2 and USB keyboards
and mice behind a single event model. Both keyboard sources push `input_event`s
into one ring queue and route characters through one function (`tty_input_char`)
into the TTY line discipline; both mouse sources funnel through `mouse_process`
into one `mouse_event` ring. Userland reads cooked lines from the TTY and raw
events through input syscalls.

Boot wiring (`kernel/src/kernel.c`): the TTY/console come up early (Prompt 4),
PS/2 + the keyboard layer come up in Prompt 5, and the unified layer is finalized
in Prompt 6:

```c
console_init(); tty_init();          /* "[OK] TTY layer online"            */
...
ps2_init(); keyboard_init();         /* "[OK] PS/2 controller online" etc. */
tty_enable_canonical();              /* "[OK] TTY interactive input online"*/
...
unified_input_init();                /* "[OK] Unified input stack online"  */
```

## Architecture

```
  PS/2 keyboard                              USB keyboard (HID)
  IRQ1 -> vector 0x21                        interrupt-IN report
  ps2_irq_handler                            hid_keyboard_handle_report
  ps2_keyboard_handle_scancode               input_emit_key / input_emit_text
        |  (set-1 decode, g_shift)                 |
        |  input_queue_push(INPUT_EV_KEY)          | input_queue_push(KEY_DOWN/UP, TEXT)
        |  keymap_us_translate -> keyboard_emit_char
        |                                          |
        +------------------+-----------------------+
                           v
                    tty_input_char(c)   <----- the single convergence point
                    (canonical line edit, echo)
                           |
                     cooked g_input[]  --> tty_read() --> userland / shell

  PS/2 mouse (stub)        USB mouse (HID)
                           hid_mouse_handle_report
                           input_emit_mouse_move/_wheel
                                   |
                              mouse_process(dx,dy,buttons,wheel,source)
                                   |
                              mouse_event_push  --> mouse_event ring (64)
```

### PS/2 controller bring-up and IRQ path

`ps2_init` programs the 8042 controller through ports `0x60` (data) and `0x64`
(status/command): it initializes the input queue and keyboard/mouse layers,
disables both ports (commands `0xAD`/`0xA7`), drains the output buffer, reads the
config byte (command `0x20`), sets bit 0 (port-1 IRQ enable) and clears bit 4
(port-1 clock disable), writes it back (command `0x60`), re-enables port 1
(`0xAE`), registers `ps2_irq_handler` on vector `0x21`, and routes IRQ line 1 to
the LAPIC via `ioapic_route_irq(1, 0x21, lapic_id)`. From then on a keypress
raises IRQ1; `ps2_irq_handler` spins while the status output-buffer bit is set,
reads each scancode from `0x60`, and calls `ps2_keyboard_handle_scancode`.

`ps2_keyboard_handle_scancode` decodes scancode set 1: `0x2A`/`0x36` set the
shift state and `(… | 0x80)` clear it (no event emitted for the shift keys
themselves); a scancode with `SC_RELEASE_BIT` (0x80) becomes an `INPUT_EV_KEY`
release event (code masked to `& 0x7F`, `value = 0`); any other scancode becomes
a press event (`value = 1`) and, after pushing to the queue, is translated via
`keymap_us_translate(sc, g_shift)` and fed to `keyboard_emit_char` (→
`tty_input_char`). Every event is tagged `INPUT_SRC_PS2_KEYBOARD`.

### Canonical line editing walkthrough

`tty_input_char` implements cooked-mode editing. Typing `h`, `i`, `x` appends
each to `g_line` and echoes it; a `\b` (or 0x7F) while `g_line_len > 0`
decrements the length and echoes `\b space \b` to visually erase; `\n`/`\r`
commits `g_line` plus a trailing newline to the cooked `g_input` buffer, echoes a
newline, and resets `g_line_len`. `tty_read` then copies from `g_input` at
`g_input_pos` and advances the cursor, returning 0 at EOF. The boot scripts in
`kernel_main` (`tty_push_line("ls /")`, …) pre-fill `g_input` through
`tty_push_input`, so the shell consumes scripted and live keystrokes through the
identical `tty_read` path.

**Two queues, one keyboard text path.** Keyboard events go to `input_queue`
(`INPUT_EV_KEY`, `INPUT_EVENT_KEY_DOWN/UP`, `INPUT_EVENT_TEXT`) and, for
printable characters, also to `tty_input_char`. Mouse events go to the separate
`mouse_event` ring. The convergence point that makes PS/2 and USB
indistinguishable to the shell is `tty_input_char`, reached from the PS/2 path
via `keyboard_emit_char` and from the USB path via `input_emit_text`.

## File map

| File | Responsibility |
|------|----------------|
| `kernel/input/input_event.c/.h` | `struct input_event`, event-type / source enums, `input_event_init_key`, `input_source_name`. |
| `kernel/input/input_queue.c/.h` | IRQ-safe 128-entry event ring (`input_queue_push/pop/count`). |
| `kernel/input/ps2/ps2.c/.h` | PS/2 controller init, IRQ1 handler, port macros. |
| `kernel/input/ps2/ps2_keyboard.c/.h` | set-1 scancode decode, shift tracking, event push. |
| `kernel/input/ps2/ps2_mouse.c/.h` | PS/2 mouse init (stub; USB HID supplies mice). |
| `kernel/input/keyboard/keyboard.c/.h` | `keyboard_init`, `keyboard_emit_char`, `keyboard_inject_scancode`. |
| `kernel/input/keyboard/keymap_us.c/.h` | `g_normal[128]` / `g_shift[128]` tables, `keymap_us_translate`. |
| `kernel/input/mouse/mouse.c/.h` | accumulated `mouse_state`, `mouse_process`. |
| `kernel/input/mouse/mouse_event.c/.h` | 64-entry mouse event ring + button-bit macros. |
| `kernel/input/hid/hid_input.c/.h` | `unified_input_init`, `input_emit_*` (bridge from HID). |
| `kernel/tty/tty.c/.h` | canonical line discipline, scripted-input injection, `tty_read`. |
| `kernel/tty/console.c/.h` | `/dev/console`, framebuffer + COM1 output. |
| `kernel/tests/input_tests.c` | PS/2 injection + TTY canonical self-tests. |
| `kernel/tests/input_compat_tests.c` | PS/2-still-works + USB keyboard/mouse + unified TTY self-tests. |

## Data structures

### `struct input_event` (`input_event.h`)

```c
struct input_event {
    uint16_t type;     /* enum input_event_type             */
    uint16_t code;     /* scancode / keycode / char / button*/
    int32_t  value;    /* press(1)/release(0) / dx / wheel   */
    int32_t  value2;   /* dy (mouse move)                    */
    uint16_t source;   /* enum input_source                 */
    uint16_t _pad;
};
```

`enum input_event_type`: `INPUT_EV_KEY` (=1, legacy: code=scancode,
value=press), `INPUT_EVENT_KEY_DOWN`, `INPUT_EVENT_KEY_UP`, `INPUT_EVENT_TEXT`,
`INPUT_EVENT_MOUSE_MOVE`, `INPUT_EVENT_MOUSE_BUTTON`, `INPUT_EVENT_MOUSE_WHEEL`.

`enum input_source`: `INPUT_SRC_UNKNOWN` (0), `INPUT_SRC_PS2_KEYBOARD`,
`INPUT_SRC_USB_KEYBOARD`, `INPUT_SRC_USB_MOUSE` (names via `input_source_name`).

### `struct mouse_event` / `struct mouse_state`

```c
struct mouse_event { int16_t dx; int16_t dy; int8_t wheel; uint8_t buttons; uint16_t source; };
struct mouse_state { int32_t x, y; uint8_t buttons; int32_t wheel; };
```

Button bits: `MOUSE_BTN_LEFT` 0x01, `MOUSE_BTN_RIGHT` 0x02, `MOUSE_BTN_MIDDLE`
0x04.

### Rings

- Input queue (`input_queue.c`): `g_ring[QUEUE_SIZE]` with `QUEUE_SIZE` = 128,
  `g_head` (read), `g_tail` (write), `g_count`. Push/pop wrap the IRQ-save flags.
- Mouse ring (`mouse_event.c`): `g_ring[MOUSE_RING]` with `MOUSE_RING` = 64.

### PS/2 + keymap

- Ports (`ps2.h`): `PS2_DATA` 0x60, `PS2_STATUS` 0x64, `PS2_CMD` 0x64; IRQ vector
  `PS2_IRQ_VECTOR` = 0x21.
- Scancode constants (`keymap_us.h`): `SC_LSHIFT` 0x2A, `SC_RSHIFT` 0x36,
  `SC_ENTER` 0x1C, `SC_BACKSPACE` 0x0E, `SC_RELEASE_BIT` 0x80.
- Tables `g_normal[128]` / `g_shift[128]` map set-1 scancodes to ASCII (Enter →
  `\n`, Backspace → `\b`, Tab → `\t`).

### TTY buffers (`tty.c`)

`TTY_INPUT_CAPACITY` = 4096 (`g_input` cooked buffer, `g_input_len`/`g_input_pos`
cursors); `TTY_LINE_MAX` = 256 (`g_line` current-line edit buffer, `g_line_len`).

## Key APIs

### Event model

- `void input_event_init_key(ev, code, pressed)` — build a legacy `INPUT_EV_KEY`.
- `const char *input_source_name(source)`.
- `void input_queue_init(void)`, `int input_queue_push(const ev*)`,
  `int input_queue_pop(ev*)`, `int input_queue_count(void)`.

### PS/2 + keyboard

- `void ps2_init(void)` — init queue + keyboard + mouse, configure the 8042
  (disable ports, flush, read config, set port-1 IRQ + clock, re-enable),
  register `ps2_irq_handler` on vector 0x21, route IRQ1 via `ioapic_route_irq`.
  Logs `PS/2 controller online`.
- `void ps2_keyboard_handle_scancode(uint8_t)` — track shift, push an
  `INPUT_EV_KEY` event (`INPUT_SRC_PS2_KEYBOARD`), and on press translate via
  `keymap_us_translate` + `keyboard_emit_char`.
- `void keyboard_init(void)` (`Keyboard input online`), `void
  keyboard_emit_char(char)` → `tty_input_char`, `void
  keyboard_inject_scancode(uint8_t)` for scripted/test injection.
- `char keymap_us_translate(scancode, shift)`.

### Mouse

- `void mouse_init(void)`, `void mouse_process(dx, dy, buttons, wheel, source)`
  (emits a move event if dx/dy nonzero, a button event on change, a wheel event
  if nonzero), `void mouse_get_state(struct mouse_state *)`.
- `void mouse_event_init(void)`, `int mouse_event_push(const ev*)`,
  `int mouse_event_pop(ev*)`, `int mouse_event_count(void)`.

### Unified bridge (`hid_input.h`)

- `void unified_input_init(void)` (`Unified input stack online`).
- `void input_emit_key/_text/_mouse_move/_mouse_button/_mouse_wheel(...)`.

### TTY (`tty.h`)

- `void tty_init(void)` (`TTY layer online`), `void tty_enable_canonical(void)`
  (`TTY interactive input online`).
- `void tty_input_char(char)` — canonical edit: `\n`/`\r` commits `g_line` (plus
  a newline) to the cooked buffer and echoes; `\b`/0x7F erases one char and
  echoes the `\b space \b` sequence; otherwise append + echo.
- `void tty_push_input(data, len)` / `void tty_push_line(line)` — inject scripted
  input into the cooked buffer (used at boot to pre-feed the shell session).
- `int64_t tty_read(buf, size)` — copy from the cooked buffer at `g_input_pos`;
  returns 0 at EOF.
- `void tty_write(buf, len)` → `console_write`; `void tty_reset_input(void)`.
- `void console_init(void)`, `void console_write(buf, len)`, `void
  console_putc(char)` (framebuffer + serial).

## Invariants

- **One keyboard text path.** Every printable keystroke, PS/2 or USB, reaches
  `tty_input_char`. PS/2 reaches it via `keyboard_emit_char`; USB via
  `input_emit_text`. The shell cannot tell the sources apart.
- **Source-tagged events.** Every `input_event`/`mouse_event` carries an
  `input_source`, so consumers can filter by device while the queues stay shared.
- **IRQ-safe queues.** `input_queue_push/pop` and `mouse_event_push/pop` mask
  interrupts (save/disable/restore flags) so the IRQ1 producer and a consumer
  never corrupt the ring.
- **Canonical (cooked) mode.** Input is line-buffered with echo and backspace
  editing; a line is only visible to `tty_read` after Enter. Raw mode is not yet
  implemented.
- **Scripted and live input share the cooked buffer.** `tty_push_line` (boot
  scripts) and `tty_input_char` (live keys) both append to `g_input`, so the
  shell consumes them uniformly.
- **PS/2 mouse is a stub.** `ps2_mouse_init` is intentionally minimal; mice are
  provided by USB HID.

## Console output path

Echo and program output share one sink. `console_init` registers `/dev/console`
as a `struct char_device` (`device_register_char`) and logs `/dev/console
online`. `console_putc` writes each byte to *both* the framebuffer console
(`fbcon_putc`) and COM1 (`serial_write_char`), so everything the TTY echoes and
everything userland writes to `/dev/console` appears on screen and on the serial
log the verifier captures. `tty_write` simply forwards to `console_write`. This
is why the boot-time shell session and every `[PASS]` marker land on the same
COM1 stream that `tools/verify_qemu.py` greps.

## Failure modes

- Ring overflow drops events silently: `input_queue_push` returns -1 when
  `g_count >= 128`; `mouse_event_push` returns -1 when full or on a NULL event.
- `input_queue_pop` / `mouse_event_pop` return -1 when empty (callers must check).
- `tty_read` returns 0 (EOF) when the cooked buffer is exhausted or `size == 0`.
- `tty_input_char` stops accepting once the line reaches `TTY_LINE_MAX - 1`;
  `tty_push_input` truncates at `TTY_INPUT_CAPACITY`.
- `keymap_us_translate` returns 0 for scancodes ≥ 128 or keys with no character.
- No panics in the input path; bad input degrades to dropped/ignored events.

## Verification

```
make verify-keyboard       # PS/2 + interactive bring-up
make verify-tty            # canonical input + shell smoke
make verify-input-unified  # PS/2 + USB unified path
```

`verify-keyboard` asserts `[OK] PS/2 controller online`, `[OK] Keyboard input
online`, `[PASS] keyboard scripted injection`. `verify-tty` asserts `[OK] TTY
interactive input online`, `[PASS] tty canonical input`, `[PASS] shell
interactive smoke`. `verify-input-unified` asserts `[OK] Unified input stack
online`, `[PASS] ps2 keyboard still works`, `[PASS] usb keyboard works`,
`[PASS] usb mouse works`, `[PASS] tty accepts unified keyboard input`.

Backing self-tests:

- `input_tests.c`: injects set-1 scancodes `0x1E/0x30/0x2E`, decodes via
  `keymap_us_translate`, asserts `a`,`b`,`c` (`[PASS] keyboard scripted
  injection`); types `h i x \b \n` and asserts `tty_read` returns the 3-byte
  cooked line `"hi\n"` (`[PASS] tty canonical input`).
- `input_compat_tests.c`: re-runs the PS/2 path checking `source ==
  INPUT_SRC_PS2_KEYBOARD`; drives a USB HID keyboard report and asserts a
  `KEY_DOWN` + `TEXT` event tagged `INPUT_SRC_USB_KEYBOARD`; drives a USB mouse
  report `{0x02,-3,7,0}` and asserts a `mouse_event` with `dx == -3`, `dy == 7`,
  `source == INPUT_SRC_USB_MOUSE`; types `"hi\n"` via HID usage codes and asserts
  the unified cooked line.

QEMU attaches a `usb-kbd` and `usb-mouse` on the xHCI root hub, so the USB paths
run against real enumerated devices. `make verify-prompt6` runs all input
verifications together.

## Future expansion

- **Raw / non-canonical TTY mode** (termios-style `ICANON`, `ECHO`, signal chars)
  so editors and games can read keystrokes immediately.
- **Real PS/2 mouse** — implement the 3-byte packet decoder (overflow/sign bits,
  X/Y movement) in `ps2_mouse.c` and route IRQ12.
- **Blocking reads + waitqueues.** `tty_read` is non-blocking against a cooked
  buffer; a real terminal needs a blocking read backed by a waitqueue woken by
  the input producer.
- **Multiple TTYs / virtual consoles** and a `/dev/input/*` event interface
  exposing the raw `input_event` stream to userland.
- **Keymap layers and layouts** — currently US-only with a single shift level;
  AltGr, dead keys, and loadable keymaps are the natural extension.
- **Absolute pointing devices** (tablets/touch) via `INPUT_EVENT_MOUSE_*` with
  absolute coordinates and clamping (the mouse state is purely relative today).
- **Modifier-aware translation.** Ctrl/Alt chords and the USB modifier byte are
  not yet folded into `keymap_us_translate`; a shared modifier model between the
  PS/2 and HID paths is the next refinement.
- **Lossless queues / backpressure.** The 128-entry input and 64-entry mouse
  rings drop on overflow; a larger or growable ring plus consumer wakeups would
  guarantee no event is lost under burst input.
