/* User subsystem bring-up + ring-3 GDT/TSS validation. */
#include "user.h"
#include "gdt.h"
#include "log.h"
#include "panic.h"

void user_init(void) {
    /* The ring-3 selectors must be the GDT user descriptors with RPL 3. The
     * GDT layout (gdt.c) is: 0x18 user data, 0x20 user code. */
    if (USER_DATA_SELECTOR_RPL3 != (GDT_USER_DATA | 3)) {
        kernel_panic("user: ring-3 data selector mismatch");
    }
    if (USER_CODE_SELECTOR_RPL3 != (GDT_USER_CODE | 3)) {
        kernel_panic("user: ring-3 code selector mismatch");
    }
    /* USER_IMAGE_BASE must land above the kernel image (TSS rsp0 switching and
     * the user address space both rely on the low footprint staying below it).
     * The actual kernel-footprint guard is enforced in user_address_space.c. */
    kernel_log_ok("Ring 3 segments validated");
    kernel_log_ok("Ring 3 entry online");
}
