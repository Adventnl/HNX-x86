/* Tiny user-space test harness mirroring kernel ktest.h. A test declares
 * UT_BEGIN(), runs UT_CHECK()/UT_CHECK_EQ() assertions, and ends with
 * UT_RESULT("marker"): on success it prints "[PASS] marker", otherwise the
 * failing condition followed by "[FAIL] marker". The verify-* targets grep for
 * the [PASS] lines, so a failed check simply makes the marker missing. */
#ifndef MYOS_USER_UTEST_H
#define MYOS_USER_UTEST_H

#include "stdio.h"

#define UT_BEGIN() int __ut_fail = 0

#define UT_CHECK(cond, msg)                                                \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("    [CHECK FAILED] %s (%s:%d)\n",                       \
                   (msg), __FILE__, __LINE__);                             \
            __ut_fail = 1;                                                 \
        }                                                                  \
    } while (0)

#define UT_CHECK_EQ(a, b, msg)                                             \
    do {                                                                   \
        long __a = (long)(a);                                              \
        long __b = (long)(b);                                              \
        if (__a != __b) {                                                  \
            printf("    [CHECK FAILED] %s: got %ld want %ld (%s:%d)\n",     \
                   (msg), __a, __b, __FILE__, __LINE__);                    \
            __ut_fail = 1;                                                 \
        }                                                                  \
    } while (0)

#define UT_RESULT(marker)                                                  \
    do {                                                                   \
        if (__ut_fail) {                                                   \
            printf("[FAIL] %s\n", (marker));                               \
        } else {                                                           \
            printf("[PASS] %s\n", (marker));                               \
        }                                                                  \
    } while (0)

#define UT_FAILED() (__ut_fail)

#endif /* MYOS_USER_UTEST_H */
