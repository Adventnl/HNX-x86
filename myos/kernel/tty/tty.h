/* TTY v0: console output + a scripted input buffer.
 *
 * Prompt 4 has no keyboard yet (Prompt 5). To exercise the read path and run the
 * shell non-interactively, the kernel pre-loads a scripted command stream that
 * /dev/console serves to readers until exhausted (then EOF). The line-discipline
 * hook is a placeholder that currently passes bytes through unchanged. */
#ifndef MYOS_TTY_H
#define MYOS_TTY_H

#include "types.h"

void tty_init(void);

/* Feed bytes / a line (newline appended) into the scripted input buffer. */
void tty_push_input(const char *data, uint64_t len);
void tty_push_line(const char *line);

/* Consume up to `size` bytes of cooked input. Returns the count (0 = EOF). */
int64_t tty_read(void *buf, uint64_t size);

/* Output through the line discipline to the console sinks. */
void tty_write(const char *buf, uint64_t len);

/* ---- canonical line discipline (Prompt 5) -------------------------------- */
/* Feed one character from the keyboard. Handles echo, backspace editing, and
 * line submission (Enter pushes the completed line to readers). */
void tty_input_char(char c);

/* Clear the cooked input buffer + pending line (used between test phases). */
void tty_reset_input(void);

/* Announce interactive input is wired. Logs "[OK] TTY interactive input online". */
void tty_enable_canonical(void);

#endif /* MYOS_TTY_H */
