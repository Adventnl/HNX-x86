/* Stage 16: jump to the kernel entry with the boot_info pointer.
 *
 * UEFI code uses the Microsoft x64 ABI; the kernel uses the System V ABI.
 * The sysv_abi function-pointer cast makes the compiler emit the correct
 * call convention, placing boot_info in RDI as the kernel expects.
 */
#include "bootloader.h"

typedef void (__attribute__((sysv_abi)) *kernel_entry_t)(struct boot_info *);

void bl_jump_to_kernel(myos_u64 entry, struct boot_info *bi) {
    kernel_entry_t enter = (kernel_entry_t)(UINTN)entry;
    enter(bi);

    /* Kernel must never return; halt forever if it somehow does. */
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
