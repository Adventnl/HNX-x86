/* Minimal user stdlib: process exit + bump heap + numeric parsing. */
#ifndef MYOS_USER_STDLIB_H
#define MYOS_USER_STDLIB_H

#include "types.h"

void  exit(int code) __attribute__((noreturn));
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t count, size_t size);
int   atoi(const char *s);

#endif /* MYOS_USER_STDLIB_H */
