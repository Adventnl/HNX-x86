/* Tiny in-kernel test harness shared by the production test suites.
 *
 * A test function declares KT_BEGIN(), runs KT_CHECK() assertions, and ends
 * with KT_RESULT("marker"). On success it prints "[PASS] marker"; on any failed
 * check it prints the failing condition and then "[FAIL] marker". The verify-*
 * targets grep for the [PASS] lines, so a failure simply makes the marker
 * missing and the verification fails honestly.
 */
#ifndef MYOS_KTEST_H
#define MYOS_KTEST_H

#include "log.h"
#include "fmt.h"

#define KT_BEGIN() int __kt_fail = 0

#define KT_CHECK(cond, msg)                                              \
    do {                                                                 \
        if (!(cond)) {                                                   \
            kdprintf("    [CHECK FAILED] %s (%s:%d)\n",                  \
                     (msg), __FILE__, __LINE__);                         \
            __kt_fail = 1;                                               \
        }                                                                \
    } while (0)

#define KT_CHECK_EQ(a, b, msg)                                          \
    do {                                                                 \
        uint64_t __a = (uint64_t)(a);                                    \
        uint64_t __b = (uint64_t)(b);                                    \
        if (__a != __b) {                                                \
            kdprintf("    [CHECK FAILED] %s: got %u want %u (%s:%d)\n",  \
                     (msg), (unsigned)__a, (unsigned)__b,                \
                     __FILE__, __LINE__);                                \
            __kt_fail = 1;                                               \
        }                                                                \
    } while (0)

#define KT_RESULT(marker)                                               \
    do {                                                                 \
        if (__kt_fail) {                                                 \
            kernel_log("[FAIL] ");                                       \
            kernel_log_line(marker);                                     \
        } else {                                                         \
            kernel_log("[PASS] ");                                       \
            kernel_log_line(marker);                                     \
        }                                                                \
    } while (0)

#define KT_FAILED() (__kt_fail)

#endif /* MYOS_KTEST_H */
