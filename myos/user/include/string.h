/* Freestanding string/memory helpers for user programs. */
#ifndef MYOS_USER_STRING_H
#define MYOS_USER_STRING_H

#include "types.h"

void  *memset(void *dst, int value, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
void  *memchr(const void *s, int c, size_t n);
char  *strdup(const char *s);                       /* malloc-backed copy */
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char  *strtok_r(char *str, const char *delim, char **saveptr);

#endif /* MYOS_USER_STRING_H */
