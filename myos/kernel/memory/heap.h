/* Kernel heap (early bump allocator). */
#ifndef MYOS_HEAP_H
#define MYOS_HEAP_H

#include "types.h"

void  heap_init(void);
void *kmalloc(uint64_t size);
void *kcalloc(uint64_t count, uint64_t size);
void  kfree(void *ptr);
void  heap_dump_stats(void);

#endif /* MYOS_HEAP_H */
