/* Intel 8042 PS/2 controller. */
#ifndef MYOS_PS2_H
#define MYOS_PS2_H

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

void ps2_init(void);   /* logs "[OK] PS/2 controller online" */

#endif /* MYOS_PS2_H */
