/* Kernel assertions. */
#ifndef MYOS_ASSERT_H
#define MYOS_ASSERT_H

void kassert_fail(const char *expr, const char *file, int line) __attribute__((noreturn));

#define KASSERT(cond)                                                     \
    do {                                                                  \
        if (!(cond)) {                                                    \
            kassert_fail(#cond, __FILE__, __LINE__);                      \
        }                                                                 \
    } while (0)

#endif /* MYOS_ASSERT_H */
