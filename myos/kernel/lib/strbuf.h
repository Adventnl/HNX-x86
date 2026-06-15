/* Bounded string builder.
 *
 * Wraps a caller-provided char buffer and appends text/numbers without ever
 * overflowing. Tracks whether truncation occurred. Handy for assembling log
 * lines, /proc-style nodes and diagnostic dumps.
 */
#ifndef MYOS_LIB_STRBUF_H
#define MYOS_LIB_STRBUF_H

#include "types.h"

struct strbuf {
    char  *buf;
    size_t cap;       /* capacity including the NUL terminator */
    size_t len;       /* current string length (excluding NUL)  */
    int    truncated; /* set once an append did not fully fit    */
};

void strbuf_init(struct strbuf *sb, char *buf, size_t cap);
void strbuf_reset(struct strbuf *sb);

void strbuf_putc(struct strbuf *sb, char c);
void strbuf_puts(struct strbuf *sb, const char *s);
void strbuf_putn(struct strbuf *sb, const char *s, size_t n);

/* Append a decimal / hex number. */
void strbuf_put_u64(struct strbuf *sb, uint64_t v);
void strbuf_put_i64(struct strbuf *sb, int64_t v);
void strbuf_put_hex(struct strbuf *sb, uint64_t v);   /* "0x.." */

/* printf-style append (subset supported by kvsnprintf). */
void strbuf_printf(struct strbuf *sb, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static inline const char *strbuf_str(const struct strbuf *sb) { return sb->buf; }
static inline size_t strbuf_len(const struct strbuf *sb) { return sb->len; }

#endif /* MYOS_LIB_STRBUF_H */
