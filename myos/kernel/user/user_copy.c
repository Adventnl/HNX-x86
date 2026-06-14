/* Kernel<->user copy helpers. The active CR3 belongs to the current process, so
 * validated user addresses can be touched directly. */
#include "user_copy.h"
#include "user_address_space.h"
#include "process.h"
#include "syscall_numbers.h"
#include "paging.h"
#include "vmm.h"
#include "heap.h"
#include "cpu.h"
#include "string.h"

static struct user_address_space *current_space(void) {
    struct process *p = process_current();
    return p ? process_address_space(p) : NULL;
}

/* All user-memory access goes through the user page tables (validate + translate)
 * with the kernel CR3 active, so neither the user's data pages nor its page-table
 * pages need to be mapped in the (limited) user CR3 mirror. The window is
 * non-blocking, so holding the kernel CR3 across it is safe. */
int user_copy_from_user(void *kdst, uint64_t usrc, uint64_t n) {
    if (n == 0) {
        return 0;
    }
    struct user_address_space *space = current_space();
    if (!space) {
        return -SYS_EFAULT;
    }
    uint64_t saved = user_with_kernel_cr3();
    int ok = user_range_is_valid(space, usrc, n, 0) &&
             user_copy_from_space(space, kdst, usrc, n) == 0;
    user_restore_cr3(saved);
    return ok ? 0 : -SYS_EFAULT;
}

int user_copy_to_user(uint64_t udst, const void *ksrc, uint64_t n) {
    if (n == 0) {
        return 0;
    }
    struct user_address_space *space = current_space();
    if (!space) {
        return -SYS_EFAULT;
    }
    uint64_t saved = user_with_kernel_cr3();
    int ok = user_range_is_valid(space, udst, n, 1) &&
             user_copy_to_space(space, udst, ksrc, n) == 0;
    user_restore_cr3(saved);
    return ok ? 0 : -SYS_EFAULT;
}

char *user_copy_string_from_user(uint64_t usrc, uint64_t maxlen) {
    struct user_address_space *space = current_space();
    if (!space || maxlen == 0) {
        return NULL;
    }
    uint64_t saved = user_with_kernel_cr3();

    /* Validate + measure one byte at a time (never read past a mapped page). */
    uint64_t len = 0;
    int terminated = 0;
    while (len < maxlen) {
        if (!user_range_is_valid(space, usrc + len, 1, 0)) {
            break;
        }
        uint64_t phys = paging_translate(user_address_space_cr3(space), usrc + len);
        if (phys == PAGING_NO_MAP) {
            break;
        }
        char c = *(const char *)(uintptr_t)phys;
        len++;
        if (c == 0) {
            terminated = 1;
            break;
        }
    }

    char *buf = NULL;
    if (terminated) {
        buf = (char *)kmalloc(len);
        if (buf && user_copy_from_space(space, buf, usrc, len) != 0) {
            buf = NULL;
        }
    }
    user_restore_cr3(saved);
    return buf;
}

uint64_t user_with_kernel_cr3(void) {
    uint64_t saved = x86_read_cr3();
    paging_load_cr3(vmm_kernel_pml4());
    return saved;
}

void user_restore_cr3(uint64_t saved) {
    paging_load_cr3(saved);
}
