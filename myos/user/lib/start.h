/* MyOS user runtime: common types + the program entry contract.
 *
 * Prompt 4 programs are freestanding (no host libc). crt0.S defines _start,
 * calls main(), and passes main's return value to the exit syscall. */
#ifndef MYOS_USER_START_H
#define MYOS_USER_START_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned long long size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Provided by each program. */
int main(void);

#endif /* MYOS_USER_START_H */
