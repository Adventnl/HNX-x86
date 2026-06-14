/* Freestanding memory/string primitives for the kernel. */
#include "kernel.h"

void *memset(void *dst, int val, usize n) {
    unsigned char *d = (unsigned char *)dst;
    unsigned char v = (unsigned char)val;
    while (n--) {
        *d++ = v;
    }
    return dst;
}

void *memcpy(void *dst, const void *src, usize n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

int memcmp(const void *a, const void *b, usize n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (n--) {
        if (*pa != *pb) {
            return (int)*pa - (int)*pb;
        }
        pa++;
        pb++;
    }
    return 0;
}

usize kstrlen(const char *s) {
    usize n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

void *memmove(void *dst, const void *src, usize n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

usize strlen(const char *s) {
    return kstrlen(s);
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, usize n) {
    while (n--) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (ca == 0) {
            break;
        }
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) {
    }
    return dst;
}

char *strncpy(char *dst, const char *src, usize n) {
    usize i = 0;
    for (; i < n && src[i]; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = 0;
    }
    return dst;
}

usize strlcpy(char *dst, const char *src, usize dst_size) {
    usize srclen = kstrlen(src);
    if (dst_size) {
        usize n = (srclen < dst_size - 1) ? srclen : dst_size - 1;
        for (usize i = 0; i < n; i++) {
            dst[i] = src[i];
        }
        dst[n] = 0;
    }
    return srclen;
}

char *strchr(const char *s, int c) {
    for (; *s; s++) {
        if (*s == (char)c) {
            return (char *)s;
        }
    }
    return (c == 0) ? (char *)s : NULL;
}

const char *strrchr(const char *s, int c) {
    const char *last = NULL;
    for (; *s; s++) {
        if (*s == (char)c) {
            last = s;
        }
    }
    return (c == 0) ? s : last;
}
