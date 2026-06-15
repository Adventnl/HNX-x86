/* Minimal user stdlib: process exit + bump heap + numeric parsing. */
#ifndef MYOS_USER_STDLIB_H
#define MYOS_USER_STDLIB_H

#include "types.h"

void  exit(int code) __attribute__((noreturn));
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
int   atoi(const char *s);
long  atol(const char *s);
long  strtol(const char *s, char **endptr, int base);
int   abs(int v);
long  labs(long v);

void  qsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*cmp)(const void *, const void *));

static inline int  imin(int a, int b)   { return a < b ? a : b; }
static inline int  imax(int a, int b)   { return a > b ? a : b; }
static inline long lmin(long a, long b) { return a < b ? a : b; }
static inline long lmax(long a, long b) { return a > b ? a : b; }

#endif /* MYOS_USER_STDLIB_H */
