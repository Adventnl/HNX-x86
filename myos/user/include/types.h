/* Freestanding fixed-width types for MyOS user programs (no host libc). */
#ifndef MYOS_USER_TYPES_H
#define MYOS_USER_TYPES_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned long long size_t;
typedef long long          ssize_t;
typedef unsigned long long uintptr_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef int bool;
#define true  1
#define false 0

#endif /* MYOS_USER_TYPES_H */
