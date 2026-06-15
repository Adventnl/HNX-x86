/* Formatted output implementation (see kernel/lib/fmt.h). */
#include "fmt.h"
#include "log.h"
#include "string.h"

/* Output sink: a fixed buffer with overflow tracking. */
struct sink {
    char  *buf;
    size_t size;   /* capacity including terminator */
    size_t len;    /* logical length written so far (may exceed size-1)      */
};

static void sink_putc(struct sink *s, char c) {
    if (s->buf && s->len + 1 < s->size) {
        s->buf[s->len] = c;
    }
    s->len++;
}

static void sink_puts(struct sink *s, const char *str) {
    while (*str) {
        sink_putc(s, *str++);
    }
}

int kfmt_u64(char *buf, size_t size, uint64_t v, unsigned base, int upper) {
    static const char lower[] = "0123456789abcdef";
    static const char upperd[] = "0123456789ABCDEF";
    const char *digits = upper ? upperd : lower;
    char tmp[32];
    int n = 0;
    if (base < 2 || base > 16) {
        base = 10;
    }
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v && n < (int)sizeof(tmp));
    int out = 0;
    while (n > 0 && (size_t)(out + 1) < size) {
        buf[out++] = tmp[--n];
    }
    if (size > 0) {
        buf[out] = '\0';
    }
    return out;
}

void kfmt_hex64(char *buf, size_t size, uint64_t v) {
    if (size < 3) {
        if (size) {
            buf[0] = '\0';
        }
        return;
    }
    buf[0] = '0';
    buf[1] = 'x';
    static const char hex[] = "0123456789ABCDEF";
    size_t want = 18; /* "0x" + 16 digits */
    size_t i;
    for (i = 0; i < 16 && (i + 3) <= size; i++) {
        int shift = (15 - (int)i) * 4;
        buf[2 + i] = hex[(v >> shift) & 0xF];
    }
    size_t end = (want < size) ? want : size - 1;
    buf[end] = '\0';
}

/* Emit a number with width/flags handling. */
static void emit_number(struct sink *s, uint64_t value, int negative,
                        unsigned base, int upper, int width,
                        int zero_pad, int left_align, int plus, int space) {
    char digits[32];
    int dn = kfmt_u64(digits, sizeof(digits), value, base, upper);

    char sign = 0;
    if (negative) {
        sign = '-';
    } else if (plus) {
        sign = '+';
    } else if (space) {
        sign = ' ';
    }

    int total = dn + (sign ? 1 : 0);
    int pad = width > total ? width - total : 0;

    if (!left_align && !zero_pad) {
        for (int i = 0; i < pad; i++) sink_putc(s, ' ');
    }
    if (sign) {
        sink_putc(s, sign);
    }
    if (!left_align && zero_pad) {
        for (int i = 0; i < pad; i++) sink_putc(s, '0');
    }
    sink_puts(s, digits);
    if (left_align) {
        for (int i = 0; i < pad; i++) sink_putc(s, ' ');
    }
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct sink s = { buf, size, 0 };

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            sink_putc(&s, *p);
            continue;
        }
        p++;
        if (*p == '\0') {
            break;
        }

        /* Flags. */
        int left = 0, zero = 0, plus = 0, space = 0;
        for (;;) {
            if (*p == '-') { left = 1; p++; }
            else if (*p == '0') { zero = 1; p++; }
            else if (*p == '+') { plus = 1; p++; }
            else if (*p == ' ') { space = 1; p++; }
            else break;
        }

        /* Width. */
        int width = 0;
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        /* Length modifiers. */
        int longness = 0; /* 0 int, 1 long, 2 long long, 3 size_t */
        while (*p == 'l') { longness++; p++; }
        if (*p == 'z') { longness = 3; p++; }

        switch (*p) {
        case 'd':
        case 'i': {
            int64_t v;
            if (longness >= 2) v = va_arg(ap, long long);
            else if (longness == 1) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            int neg = v < 0;
            uint64_t mag = neg ? (uint64_t)(-(v + 1)) + 1ULL : (uint64_t)v;
            emit_number(&s, mag, neg, 10, 0, width, zero, left, plus, space);
            break;
        }
        case 'u': {
            uint64_t v;
            if (longness >= 2 || longness == 3) v = va_arg(ap, unsigned long long);
            else if (longness == 1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            emit_number(&s, v, 0, 10, 0, width, zero, left, 0, 0);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v;
            if (longness >= 2 || longness == 3) v = va_arg(ap, unsigned long long);
            else if (longness == 1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            emit_number(&s, v, 0, 16, *p == 'X', width, zero, left, 0, 0);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
            sink_puts(&s, "0x");
            emit_number(&s, v, 0, 16, 0, width, zero, left, 0, 0);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!left) for (int i = 0; i < pad; i++) sink_putc(&s, ' ');
            sink_putc(&s, c);
            if (left) for (int i = 0; i < pad; i++) sink_putc(&s, ' ');
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            int slen = (int)strlen(str);
            int pad = width > slen ? width - slen : 0;
            if (!left) for (int i = 0; i < pad; i++) sink_putc(&s, ' ');
            sink_puts(&s, str);
            if (left) for (int i = 0; i < pad; i++) sink_putc(&s, ' ');
            break;
        }
        case '%':
            sink_putc(&s, '%');
            break;
        default:
            sink_putc(&s, '%');
            sink_putc(&s, *p);
            break;
        }
    }

    if (s.size > 0) {
        size_t term = (s.len < s.size) ? s.len : s.size - 1;
        s.buf[term] = '\0';
    }
    return (int)s.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int kdprintf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kernel_log(buf);
    return n;
}
