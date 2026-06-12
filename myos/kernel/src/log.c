/* Kernel logging router: fans out to the framebuffer console and serial.
 * Never crashes if either sink is unavailable. */
#include "log.h"
#include "framebuffer_console.h"
#include "serial.h"

static int g_initialized = 0;

void kernel_log_init(void) {
    g_initialized = 1;
}

static void sink(const char *s) {
    if (fbcon_ready()) {
        fbcon_puts(s);
    }
    if (serial_is_ready()) {
        serial_write_string(s);
    }
    (void)g_initialized;
}

void kernel_log(const char *message) {
    if (message) {
        sink(message);
    }
}

void kernel_log_line(const char *message) {
    kernel_log(message);
    sink("\n");
}

void kernel_log_ok(const char *message) {
    sink("[OK] ");
    kernel_log(message);
    sink("\n");
}

void kernel_log_warn(const char *message) {
    sink("[WARN] ");
    kernel_log(message);
    sink("\n");
}

void kernel_log_error(const char *message) {
    sink("[ERROR] ");
    kernel_log(message);
    sink("\n");
}

void kernel_log_hex64(const char *label, uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[2 + i] = digits[(value >> ((15 - i) * 4)) & 0xF];
    }
    buf[18] = 0;
    kernel_log(label);
    sink(buf);
    sink("\n");
}
