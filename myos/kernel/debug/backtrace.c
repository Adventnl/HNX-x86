/* Stack backtrace implementation (see kernel/debug/backtrace.h). */
#include "backtrace.h"
#include "ksym.h"
#include "log.h"
#include "fmt.h"
#include "memory_layout.h"

/* Sanity bound: a kernel code/return address lives in the higher half or the
 * low identity-mapped image. Reject obviously bogus frame links. */
static int plausible_code_addr(uint64_t a) {
    return a >= KERNEL_PHYSICAL_BASE;
}

static int plausible_stack_ptr(uint64_t p) {
    /* Non-zero, 8-byte aligned, and canonical. */
    return p != 0 && (p & 0x7) == 0;
}

size_t backtrace_capture(uint64_t *frames, size_t max, uint64_t start_rbp) {
    uint64_t rbp = start_rbp;
    if (rbp == 0) {
        __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    }
    size_t n = 0;
    uint64_t prev = 0;
    while (n < max && plausible_stack_ptr(rbp)) {
        if (rbp <= prev) {
            /* Frame pointers must grow upward; stop on corruption/loops. */
            if (prev != 0) {
                break;
            }
        }
        uint64_t *fp = (uint64_t *)(uintptr_t)rbp;
        uint64_t ret = fp[1];
        if (!plausible_code_addr(ret)) {
            break;
        }
        frames[n++] = ret;
        prev = rbp;
        rbp = fp[0];
    }
    return n;
}

void backtrace_print_from(uint64_t rbp, const char *label) {
    uint64_t frames[BACKTRACE_MAX_FRAMES];
    size_t n = backtrace_capture(frames, BACKTRACE_MAX_FRAMES, rbp);
    kdprintf("---- backtrace: %s (%u frames) ----\n",
             label ? label : "", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        uint64_t off = 0;
        const char *name = ksym_resolve(frames[i], &off);
        if (name) {
            kdprintf("  #%u %p %s+0x%x\n", (unsigned)i, (void *)frames[i],
                     name, (unsigned)off);
        } else {
            kdprintf("  #%u %p\n", (unsigned)i, (void *)frames[i]);
        }
    }
}

void backtrace_print(const char *label) {
    uint64_t rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    backtrace_print_from(rbp, label);
}
