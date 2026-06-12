/* Kernel panic / halt hardening. */
#include "panic.h"
#include "kernel.h"
#include "log.h"
#include "framebuffer_console.h"
#include "cpu.h"

void kernel_halt_forever(void) {
    x86_cli();
    for (;;) {
        x86_halt();
    }
}

void kernel_panic(const char *message) {
    x86_cli();
    if (fbcon_ready()) {
        fbcon_set_color(0x00FF5555, 0x00000000);   /* red on black */
    }
    kernel_log_line("");
    kernel_log("[PANIC] ");
    kernel_log_line(message ? message : "(null)");
    kernel_halt_forever();
}

void kernel_panic_hex(const char *message, uint64_t value) {
    x86_cli();
    if (fbcon_ready()) {
        fbcon_set_color(0x00FF5555, 0x00000000);
    }
    kernel_log_line("");
    kernel_log("[PANIC] ");
    kernel_log(message ? message : "(null)");
    kernel_log(" ");
    kernel_log_hex64("", value);
    kernel_halt_forever();
}

/* ---- Backward-compatible stage 1-16 entry points ------------------------- */
void khalt(void) {
    kernel_halt_forever();
}

void panic(const char *msg) {
    kernel_panic(msg);
}
