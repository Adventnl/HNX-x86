/* /dev/console: framebuffer + serial output, scripted TTY input. */
#include "console.h"
#include "tty.h"
#include "char_device.h"
#include "device.h"
#include "framebuffer_console.h"
#include "serial.h"
#include "log.h"

void console_putc(char c) {
    fbcon_putc(c);
    serial_write_char(c);
}

void console_write(const char *buf, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        console_putc(buf[i]);
    }
}

static int64_t console_dev_read(struct char_device *cd, void *buf, uint64_t size) {
    (void)cd;
    return tty_read(buf, size);   /* scripted input; 0 = EOF */
}

static int64_t console_dev_write(struct char_device *cd, const void *buf, uint64_t size) {
    (void)cd;
    console_write((const char *)buf, size);
    return (int64_t)size;
}

static struct char_device g_console = {
    "console", console_dev_read, console_dev_write, NULL
};

void console_init(void) {
    device_register_char(&g_console);
    kernel_log_ok("/dev/console online");
}
