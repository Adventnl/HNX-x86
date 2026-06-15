/* Minimal stdio over the write syscall: print helpers + a small printf. */
#include "stdio.h"
#include "string.h"
#include "unistd.h"

long print(const char *s) {
    return write(1, s, strlen(s));
}

long eprint(const char *s) {
    return write(2, s, strlen(s));
}

int putchar(int c) {
    char ch = (char)c;
    write(1, &ch, 1);
    return c;
}

int puts(const char *s) {
    print(s);
    write(1, "\n", 1);
    return 0;
}

int fputs(const char *s, int fd) {
    return (int)write(fd, s, strlen(s));
}

static int u64_to_str(uint64_t v, char *out, int base, int upper) {
    char tmp[32];
    int i = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) {
        tmp[i++] = '0';
    }
    while (v) {
        tmp[i++] = digits[v % (unsigned)base];
        v /= (unsigned)base;
    }
    for (int j = 0; j < i; j++) {
        out[j] = tmp[i - 1 - j];
    }
    out[i] = 0;
    return i;
}

void print_u64(uint64_t v) {
    char b[24];
    u64_to_str(v, b, 10, 0);
    write(1, b, strlen(b));
}

void print_i64(int64_t v) {
    if (v < 0) {
        write(1, "-", 1);
        print_u64((uint64_t)(-v));
    } else {
        print_u64((uint64_t)v);
    }
}

/* ---- printf core ----------------------------------------------------------
 * A single formatter feeds either an fd (printf) via an internal flush buffer,
 * or a caller-supplied bounded buffer (snprintf). */
#define PBUF 256
struct out {
    char  buf[PBUF];   /* flush buffer when writing to an fd */
    int   len;
    int   total;       /* characters that would be written (snprintf semantics) */
    int   fd;          /* >=0: write to this fd; <0: write to dst buffer */
    char *dst;         /* destination buffer (snprintf) or NULL */
    size_t cap;        /* dst capacity including room for NUL */
};

static void oflush(struct out *o) {
    if (o->fd >= 0 && o->len) {
        write(o->fd, o->buf, (unsigned long)o->len);
        o->len = 0;
    }
}
static void oputc(struct out *o, char c) {
    if (o->fd >= 0) {
        if (o->len == PBUF) {
            oflush(o);
        }
        o->buf[o->len++] = c;
    } else if (o->dst && (size_t)o->total + 1 < o->cap) {
        o->dst[o->total] = c;
    }
    o->total++;
}
static int field_len(const char *s) {
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

/* Emit `s` (length len) in a field of `width`, padding with `pad` (or spaces). */
static void emit_field(struct out *o, const char *s, int len,
                       int width, int left, char pad) {
    if (!left) {
        for (int i = len; i < width; i++) {
            oputc(o, pad);
        }
    }
    for (int i = 0; i < len; i++) {
        oputc(o, s[i]);
    }
    if (left) {
        for (int i = len; i < width; i++) {
            oputc(o, ' ');
        }
    }
}

static int vformat(struct out *o, const char *fmt, __builtin_va_list ap) {
    char num[40];

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            oputc(o, *fmt);
            continue;
        }
        fmt++;

        int left = 0, zero = 0;
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else break;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        int lng = 0;
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }
        char pad = (zero && !left) ? '0' : ' ';

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            emit_field(o, s, field_len(s), width, left, ' ');
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            emit_field(o, &c, 1, width, left, ' ');
            break;
        }
        case 'd':
        case 'i': {
            int64_t v = lng ? __builtin_va_arg(ap, long)
                            : (int64_t)__builtin_va_arg(ap, int);
            char tmp[40];
            int len;
            if (v < 0) {
                tmp[0] = '-';
                len = 1 + u64_to_str((uint64_t)(-v), tmp + 1, 10, 0);
            } else {
                len = u64_to_str((uint64_t)v, tmp, 10, 0);
            }
            emit_field(o, tmp, len, width, left, pad);
            break;
        }
        case 'u': {
            uint64_t v = lng ? __builtin_va_arg(ap, unsigned long)
                             : (uint64_t)__builtin_va_arg(ap, unsigned int);
            int len = u64_to_str(v, num, 10, 0);
            emit_field(o, num, len, width, left, pad);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t v = lng ? __builtin_va_arg(ap, unsigned long)
                             : (uint64_t)__builtin_va_arg(ap, unsigned int);
            int len = u64_to_str(v, num, 16, *fmt == 'X');
            emit_field(o, num, len, width, left, pad);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)__builtin_va_arg(ap, void *);
            num[0] = '0';
            num[1] = 'x';
            int len = 2 + u64_to_str(v, num + 2, 16, 0);
            emit_field(o, num, len, width, left, ' ');
            break;
        }
        case '%':
            oputc(o, '%');
            break;
        case 0:
            goto done;
        default:
            oputc(o, '%');
            oputc(o, *fmt);
            break;
        }
    }
done:
    oflush(o);
    return o->total;
}

int printf(const char *fmt, ...) {
    struct out o;
    o.len = 0;
    o.total = 0;
    o.fd = 1;
    o.dst = NULL;
    o.cap = 0;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vformat(&o, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    struct out o;
    o.len = 0;
    o.total = 0;
    o.fd = -1;
    o.dst = buf;
    o.cap = size;
    int n = vformat(&o, fmt, ap);
    if (buf && size > 0) {
        size_t end = ((size_t)n < size) ? (size_t)n : size - 1;
        buf[end] = 0;
    }
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

/* Read one line (terminated by '\n' or EOF) from fd, byte at a time so we never
 * over-read on a pipe/tty. Returns bytes stored; 0 = EOF, <0 = error. */
long fdgets(int fd, char *buf, size_t size) {
    if (size == 0) {
        return 0;
    }
    size_t i = 0;
    while (i + 1 < size) {
        char c;
        long r = read(fd, &c, 1);
        if (r < 0) {
            return r;
        }
        if (r == 0) {
            break;          /* EOF */
        }
        buf[i++] = c;
        if (c == '\n') {
            break;
        }
    }
    buf[i] = 0;
    return (long)i;
}
