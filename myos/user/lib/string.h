/* Minimal freestanding string/memory helpers for user programs. */
#ifndef MYOS_USER_STRING_H
#define MYOS_USER_STRING_H

#include "start.h"

void  *memset(void *dst, int value, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif /* MYOS_USER_STRING_H */
