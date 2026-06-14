/* Console: the /dev/console character device. Output goes to the framebuffer
 * console and COM1; input is served from the TTY scripted buffer. */
#ifndef MYOS_TTY_CONSOLE_H
#define MYOS_TTY_CONSOLE_H

#include "types.h"

/* Register /dev/console with the device registry. Logs "[OK] /dev/console online". */
void console_init(void);

void console_write(const char *buf, uint64_t len);
void console_putc(char c);

#endif /* MYOS_TTY_CONSOLE_H */
