/* assert(): print the failed expression with file/line, then exit(1). Define
 * NDEBUG before including to compile assertions out. */
#ifndef MYOS_USER_ASSERT_H
#define MYOS_USER_ASSERT_H

#include "stdio.h"
#include "stdlib.h"

#ifdef NDEBUG
#define assert(expr) ((void)0)
#else
#define assert(expr)                                                       \
    do {                                                                   \
        if (!(expr)) {                                                     \
            printf("assertion failed: %s (%s:%d)\n",                       \
                   #expr, __FILE__, __LINE__);                             \
            exit(1);                                                       \
        }                                                                  \
    } while (0)
#endif

#endif /* MYOS_USER_ASSERT_H */
