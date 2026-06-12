/* Early COM1 serial output. */
#ifndef MYOS_X86_SERIAL_H
#define MYOS_X86_SERIAL_H

#include "types.h"

#define SERIAL_COM1_BASE 0x3F8

void serial_init(void);
int  serial_is_ready(void);
void serial_write_char(char c);
void serial_write_string(const char *text);
void serial_write_hex64(uint64_t value);

#endif /* MYOS_X86_SERIAL_H */
