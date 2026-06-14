/* TTY v0 implementation: a linear scripted-input buffer + pass-through output. */
#include "tty.h"
#include "console.h"
#include "string.h"
#include "log.h"

#define TTY_INPUT_CAPACITY 4096

static char     g_input[TTY_INPUT_CAPACITY];
static uint64_t g_input_len;       /* bytes written into the buffer */
static uint64_t g_input_pos;       /* read cursor */

void tty_init(void) {
    g_input_len = 0;
    g_input_pos = 0;
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
    /* Line-discipline placeholder: no echo/cooking yet, straight to console. */
    console_write(buf, len);
}
