/* Minimal fixed-width types for the freestanding kernel. */
#ifndef MYOS_KERNEL_TYPES_H
#define MYOS_KERNEL_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed   char      i8;
typedef short              i16;
typedef int                i32;
typedef long long          i64;

/* stdint-style aliases (used by the Prompt 2 arch/memory subsystems). */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed   char      int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;

typedef unsigned long long usize;
typedef long long          isize;
typedef unsigned long long size_t;
typedef unsigned long long uintptr_t;

typedef unsigned char      bool;
#define true  1
#define false 0

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif /* MYOS_KERNEL_TYPES_H */
