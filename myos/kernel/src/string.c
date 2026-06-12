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
