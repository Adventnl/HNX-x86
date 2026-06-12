/* Kernel logging router: writes to framebuffer console and serial. */
#ifndef MYOS_LOG_H
#define MYOS_LOG_H

#include "types.h"

void kernel_log_init(void);

void kernel_log(const char *message);        /* raw, no newline           */
void kernel_log_line(const char *message);   /* message + newline         */
void kernel_log_ok(const char *message);     /* "[OK] " + message + nl     */
void kernel_log_warn(const char *message);   /* "[WARN] " + message + nl   */
void kernel_log_error(const char *message);  /* "[ERROR] " + message + nl  */
void kernel_log_hex64(const char *label, uint64_t value);

#endif /* MYOS_LOG_H */
