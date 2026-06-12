/* CPU state inspection helpers. */
#include "cpu.h"
#include "log.h"

void cpu_read_gdtr(struct x86_descriptor_ptr *out) {
    __asm__ volatile("sgdt %0" : "=m"(*out));
}

void cpu_read_idtr(struct x86_descriptor_ptr *out) {
    __asm__ volatile("sidt %0" : "=m"(*out));
}

int cpu_state_readable(void) {
    /* Touch a couple of control registers; if we are executing this at all
     * in long mode they must be readable. */
    volatile uint64_t cr0 = x86_read_cr0();
    volatile uint64_t cr4 = x86_read_cr4();
    (void)cr0;
    (void)cr4;
    return 1;
}

void cpu_dump_state(void) {
    kernel_log_hex64("    cr0    = ", x86_read_cr0());
    kernel_log_hex64("    cr3    = ", x86_read_cr3());
    kernel_log_hex64("    cr4    = ", x86_read_cr4());
    kernel_log_hex64("    rflags = ", x86_read_rflags());
}
