/* Legacy 8259 PIC: remap away from the CPU-exception vector range, then mask
 * every line. The kernel uses the Local APIC; the PIC stays disabled unless
 * the PIT-fallback timer path explicitly unmasks IRQ0. */
#include "pic.h"
#include "cpu.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01
#define PIC_EOI   0x20

/* Tiny delay between PIC writes (old hardware needs it; harmless in QEMU). */
static void io_wait(void) {
    x86_outb(0x80, 0);
}

void pic_remap(uint8_t master_offset, uint8_t slave_offset) {
    uint8_t m1 = x86_inb(PIC1_DATA);   /* preserve masks */
    uint8_t m2 = x86_inb(PIC2_DATA);

    x86_outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    x86_outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    x86_outb(PIC1_DATA, master_offset);        io_wait();   /* ICW2: vector base */
    x86_outb(PIC2_DATA, slave_offset);         io_wait();
    x86_outb(PIC1_DATA, 0x04);                 io_wait();   /* ICW3: slave on IRQ2 */
    x86_outb(PIC2_DATA, 0x02);                 io_wait();
    x86_outb(PIC1_DATA, ICW4_8086);            io_wait();
    x86_outb(PIC2_DATA, ICW4_8086);            io_wait();

    x86_outb(PIC1_DATA, m1);
    x86_outb(PIC2_DATA, m2);
}

void pic_mask_all(void) {
    x86_outb(PIC1_DATA, 0xFF);
    x86_outb(PIC2_DATA, 0xFF);
}

void pic_disable(void) {
    /* Remap first so any spurious legacy IRQ that slips through lands in the
     * 0x20+ range (handled by the IRQ dispatcher) instead of corrupting the
     * CPU-exception vectors 8-15. Then mask every line. */
    pic_remap(0x20, 0x28);
    pic_mask_all();
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        x86_outb(PIC2_CMD, PIC_EOI);
    }
    x86_outb(PIC1_CMD, PIC_EOI);
}

void pic_unmask_irq(uint8_t irq) {
    if (irq < 8) {
        uint8_t m = x86_inb(PIC1_DATA);
        x86_outb(PIC1_DATA, (uint8_t)(m & ~(1u << irq)));
    } else if (irq < 16) {
        uint8_t m = x86_inb(PIC2_DATA);
        x86_outb(PIC2_DATA, (uint8_t)(m & ~(1u << (irq - 8))));
        uint8_t c = x86_inb(PIC1_DATA);
        x86_outb(PIC1_DATA, (uint8_t)(c & ~(1u << 2)));   /* cascade line */
    }
}
