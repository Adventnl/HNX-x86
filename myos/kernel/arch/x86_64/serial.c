/* Early COM1 (0x3F8) serial logging, 38400 8N1.
 * Works with QEMU `-serial stdio`. */
#include "serial.h"
#include "cpu.h"

#define COM1 SERIAL_COM1_BASE

/* Register offsets. */
#define REG_DATA        0  /* DLAB=0: data            */
#define REG_IER         1  /* DLAB=0: interrupt enable */
#define REG_DIVISOR_LO  0  /* DLAB=1                  */
#define REG_DIVISOR_HI  1  /* DLAB=1                  */
#define REG_FCR         2  /* FIFO control            */
#define REG_LCR         3  /* line control            */
#define REG_MCR         4  /* modem control           */
#define REG_LSR         5  /* line status             */

#define LSR_THR_EMPTY   0x20

static int g_ready = 0;

void serial_init(void) {
    x86_outb(COM1 + REG_IER, 0x00);       /* disable interrupts          */
    x86_outb(COM1 + REG_LCR, 0x80);       /* enable DLAB                 */
    x86_outb(COM1 + REG_DIVISOR_LO, 0x03);/* 38400 baud (divisor 3)      */
    x86_outb(COM1 + REG_DIVISOR_HI, 0x00);
    x86_outb(COM1 + REG_LCR, 0x03);       /* 8 bits, no parity, 1 stop   */
    x86_outb(COM1 + REG_FCR, 0xC7);       /* enable+clear FIFO, 14-byte  */
    x86_outb(COM1 + REG_MCR, 0x0B);       /* DTR, RTS, OUT2              */
    g_ready = 1;
}

int serial_is_ready(void) {
    return g_ready;
}

static void wait_thr(void) {
    /* Bounded spin so a missing/broken UART can never hang the kernel. */
    for (int i = 0; i < 100000; i++) {
        if (x86_inb(COM1 + REG_LSR) & LSR_THR_EMPTY) {
            return;
        }
    }
}

void serial_write_char(char c) {
    if (!g_ready) {
        return;
    }
    if (c == '\n') {
        wait_thr();
        x86_outb(COM1 + REG_DATA, '\r');
    }
    wait_thr();
    x86_outb(COM1 + REG_DATA, (uint8_t)c);
}

void serial_write_string(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        serial_write_char(*text++);
    }
}

void serial_write_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write_char('0');
    serial_write_char('x');
    for (int i = 15; i >= 0; i--) {
        serial_write_char(digits[(value >> (i * 4)) & 0xF]);
    }
}
