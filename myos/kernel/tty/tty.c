/* TTY v0 implementation: a linear scripted-input buffer + pass-through output. */
#include "tty.h"
#include "console.h"
#include "string.h"
#include "log.h"

#define TTY_INPUT_CAPACITY 4096
#define TTY_LINE_MAX 256

static char     g_input[TTY_INPUT_CAPACITY];
static uint64_t g_input_len;       /* bytes written into the buffer */
static uint64_t g_input_pos;       /* read cursor */

/* Canonical line-editing buffer (assembled from keyboard input). */
static char     g_line[TTY_LINE_MAX];
static uint64_t g_line_len;

void tty_init(void) {
    g_input_len = 0;
    g_input_pos = 0;
    g_line_len = 0;
    kernel_log_ok("TTY layer online");
}

void tty_push_input(const char *data, uint64_t len) {
    for (uint64_t i = 0; i < len && g_input_len < TTY_INPUT_CAPACITY; i++) {
        g_input[g_input_len++] = data[i];
    }
}

void tty_push_line(const char *line) {
    tty_push_input(line, strlen(line));
    tty_push_input("\n", 1);
}

int64_t tty_read(void *buf, uint64_t size) {
    if (g_input_pos >= g_input_len || size == 0) {
        return 0;   /* EOF / no scripted input remaining */
    }
    uint64_t avail = g_input_len - g_input_pos;
    uint64_t n = (size < avail) ? size : avail;
    memcpy(buf, &g_input[g_input_pos], n);
    g_input_pos += n;
    return (int64_t)n;
}

void tty_write(const char *buf, uint64_t len) {
    console_write(buf, len);
}

/* Canonical (cooked) input: echo, backspace editing, line submission on Enter. */
void tty_input_char(char c) {
    if (c == '\n' || c == '\r') {
        tty_push_input(g_line, g_line_len);
        tty_push_input("\n", 1);
        console_putc('\n');
        g_line_len = 0;
        return;
    }
    if (c == '\b' || c == 0x7F) {
        if (g_line_len > 0) {
            g_line_len--;
            console_putc('\b');
            console_putc(' ');
            console_putc('\b');
        }
        return;
    }
    if (g_line_len < TTY_LINE_MAX - 1) {
        g_line[g_line_len++] = c;
        console_putc(c);   /* echo */
    }
}

void tty_reset_input(void) {
    g_input_len = 0;
    g_input_pos = 0;
    g_line_len = 0;
}

void tty_enable_canonical(void) {
    kernel_log_ok("TTY interactive input online");
}
