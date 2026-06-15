/* String builder implementation (see kernel/lib/strbuf.h). */
#include "strbuf.h"
#include "fmt.h"
#include "string.h"

void strbuf_init(struct strbuf *sb, char *buf, size_t cap) {
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    sb->truncated = 0;
    if (cap > 0) {
        buf[0] = '\0';
    }
}

void strbuf_reset(struct strbuf *sb) {
    sb->len = 0;
    sb->truncated = 0;
    if (sb->cap > 0) {
        sb->buf[0] = '\0';
    }
}

void strbuf_putc(struct strbuf *sb, char c) {
    if (sb->len + 1 < sb->cap) {
        sb->buf[sb->len++] = c;
        sb->buf[sb->len] = '\0';
    } else {
        sb->truncated = 1;
    }
}

void strbuf_putn(struct strbuf *sb, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (sb->len + 1 >= sb->cap) {
            sb->truncated = 1;
            return;
        }
        sb->buf[sb->len++] = s[i];
    }
    if (sb->cap > 0) {
        sb->buf[sb->len] = '\0';
    }
}

void strbuf_puts(struct strbuf *sb, const char *s) {
    strbuf_putn(sb, s, strlen(s));
}

void strbuf_put_u64(struct strbuf *sb, uint64_t v) {
    char tmp[24];
    kfmt_u64(tmp, sizeof(tmp), v, 10, 0);
    strbuf_puts(sb, tmp);
}

void strbuf_put_i64(struct strbuf *sb, int64_t v) {
    if (v < 0) {
        strbuf_putc(sb, '-');
        strbuf_put_u64(sb, (uint64_t)(-(v + 1)) + 1ULL);
    } else {
        strbuf_put_u64(sb, (uint64_t)v);
    }
}

void strbuf_put_hex(struct strbuf *sb, uint64_t v) {
    char tmp[20];
    strbuf_puts(sb, "0x");
    kfmt_u64(tmp, sizeof(tmp), v, 16, 0);
    strbuf_puts(sb, tmp);
}

void strbuf_printf(struct strbuf *sb, const char *fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof(tmp)) {
        sb->truncated = 1;
    }
    strbuf_puts(sb, tmp);
}
