/* meminfo: report physical-memory statistics from the kernel PMM. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_meminfo m;
    if (meminfo(&m) < 0) {
        print("meminfo: error\n");
        return 1;
    }
    unsigned long ps = (unsigned long)m.page_size;
    unsigned long total_kb = (unsigned long)(m.total_pages * ps / 1024);
    unsigned long free_kb = (unsigned long)(m.free_pages * ps / 1024);
    unsigned long used_kb = (unsigned long)(m.used_pages * ps / 1024);
    printf("page size : %lu bytes\n", ps);
    printf("total     : %lu pages (%lu KiB)\n", (unsigned long)m.total_pages, total_kb);
    printf("used      : %lu pages (%lu KiB)\n", (unsigned long)m.used_pages, used_kb);
    printf("free      : %lu pages (%lu KiB)\n", (unsigned long)m.free_pages, free_kb);
    return 0;
}
