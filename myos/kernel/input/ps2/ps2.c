/* 8042 PS/2 controller init + keyboard IRQ (IRQ1 -> vector 0x21 via I/O APIC). */
#include "ps2.h"
#include "ps2_keyboard.h"
#include "ps2_mouse.h"
#include "input_queue.h"
#include "cpu.h"
#include "irq.h"
#include "ioapic.h"
#include "apic.h"
#include "log.h"

#define PS2_IRQ_VECTOR (IRQ_BASE_VECTOR + 1)   /* 0x21 */

static void flush_output(void) {
    int spin = 0;
    while ((x86_inb(PS2_STATUS) & 0x01) && spin++ < 1024) {
        (void)x86_inb(PS2_DATA);
    }
}

static void wait_input_clear(void) {
    int spin = 0;
    while ((x86_inb(PS2_STATUS) & 0x02) && spin++ < 100000) {
    }
}

static void ctrl_write(uint8_t cmd, uint8_t data) {
    wait_input_clear();
    x86_outb(PS2_CMD, cmd);
    wait_input_clear();
    x86_outb(PS2_DATA, data);
}

static uint8_t ctrl_read_config(void) {
    wait_input_clear();
    x86_outb(PS2_CMD, 0x20);
    int spin = 0;
    while (!(x86_inb(PS2_STATUS) & 0x01) && spin++ < 100000) {
    }
    return x86_inb(PS2_DATA);
}

static void ps2_irq_handler(uint8_t vector, void *ctx) {
    (void)vector; (void)ctx;
    while (x86_inb(PS2_STATUS) & 0x01) {
        uint8_t sc = x86_inb(PS2_DATA);
        ps2_keyboard_handle_scancode(sc);
    }
}

void ps2_init(void) {
    input_queue_init();
    ps2_keyboard_init();
    ps2_mouse_init();

    /* Disable ports, flush, then re-enable port 1 with IRQ generation. */
    wait_input_clear(); x86_outb(PS2_CMD, 0xAD);   /* disable port 1 */
    wait_input_clear(); x86_outb(PS2_CMD, 0xA7);   /* disable port 2 */
    flush_output();

    uint8_t cfg = ctrl_read_config();
    cfg |= 0x01;          /* enable port-1 (keyboard) interrupt */
    cfg &= ~0x10;         /* clear port-1 clock disable */
    ctrl_write(0x60, cfg);

    wait_input_clear(); x86_outb(PS2_CMD, 0xAE);   /* enable port 1 */
    flush_output();

    /* Route IRQ1 -> vector 0x21 to this CPU's LAPIC, install the handler. */
    irq_register_handler(PS2_IRQ_VECTOR, ps2_irq_handler, NULL);
    uint8_t lapic_id = (uint8_t)((lapic_read(LAPIC_REG_ID) >> 24) & 0xFF);
    ioapic_route_irq(1, PS2_IRQ_VECTOR, lapic_id);

    kernel_log_ok("PS/2 controller online");
}
