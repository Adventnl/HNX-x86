/* Legacy 8259 PIC control (remap + mask/disable). */
#ifndef MYOS_X86_PIC_H
#define MYOS_X86_PIC_H

#include "types.h"

void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_mask_all(void);
void pic_disable(void);                 /* remap away from exceptions + mask all */
void pic_send_eoi(uint8_t irq);

/* PIT-fallback support: unmask a single legacy IRQ line (0-15). */
void pic_unmask_irq(uint8_t irq);

#endif /* MYOS_X86_PIC_H */
