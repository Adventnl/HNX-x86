/* Freestanding string/memory helpers for the kernel. */
#ifndef MYOS_KERNEL_STRING_H
#define MYOS_KERNEL_STRING_H

#include "types.h"

void  *memset(void *dst, int val, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
/* Bounded copy that always NUL-terminates (dst_size includes the terminator).
 * Returns the length of src. */
size_t strlcpy(char *dst, const char *src, size_t dst_size);
char  *strchr(const char *s, int c);
const char *strrchr(const char *s, int c);

#endif /* MYOS_KERNEL_STRING_H */
