# TTY + Interactive Shell

## TTY v0 (`kernel/tty/tty.c`)

The TTY holds a cooked input ring served to `/dev/console` readers. Two
producers:

1. `tty_push_line()` — raw scripted lines (Prompt 4; still used to feed the
   shell sessions during verification).
2. `tty_input_char()` — the **canonical line discipline** fed by the keyboard:
   * printable chars are echoed and appended to a line buffer,
   * Backspace (`\b`/0x7F) erases the last char and rewrites the screen
     (`\b \b`),
   * Enter submits the line (buffer + `\n`) into the cooked ring and echoes a
     newline.

`tty_reset_input()` clears the ring + pending line (used between self-test
phases). `tty_enable_canonical()` announces `[OK] TTY interactive input online`.

Self-test: type `h i x <bs> <enter>` and confirm the cooked line is exactly
`hi\n` → `[PASS] tty canonical input`.

## Interactive shell (`user/shell/shell.c`)

* `shell` (scripted): echoes + runs each line until `exit`/EOF →
  `[PASS] shell scripted session`.
* `shell -i` (interactive): prints a `myos:<cwd>$ ` prompt (cwd from `getcwd`),
  reads a cooked line, runs it; `cd`/`exit` are in-process builtins, other
  commands resolve to `/bin/<cmd>.hxe`. Ends with
  `[PASS] shell interactive smoke`.

Both read one line at a time from stdin, so the scripted shell stops at its
`exit` and leaves the following lines for the interactive shell — letting a
single pre-loaded console stream drive both sessions deterministically.

## Prompt 6 — unified keyboard input

The TTY canonical line discipline now receives text from BOTH the PS/2 keyboard and the USB HID keyboard through the unified input bridge (`input_emit_text` calls `tty_input_char`). A self-test types "hi\n" via USB boot keyboard reports and confirms `tty_read` returns the cooked line: `[PASS] tty accepts unified keyboard input`. See [input.md](input.md).
