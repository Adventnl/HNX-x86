/* free: show physical memory usage (total/used/free) in KiB from meminfo. */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct sys_meminfo m;
    if (meminfo(&m) < 0) {
        eprint("free: error\n");
        return 1;
    }
    unsigned long ps = (unsigned long)m.page_size;
    unsigned long total = (unsigned long)(m.total_pages * ps / 1024);
    unsigned long used  = (unsigned long)(m.used_pages  * ps / 1024);
    unsigned long freek = (unsigned long)(m.free_pages  * ps / 1024);
    printf("%-10s%12s%12s%12s\n", "", "total", "used", "free");
    printf("%-10s%12lu%12lu%12lu\n", "Mem:", total, used, freek);
    return 0;
}
