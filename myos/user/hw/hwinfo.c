/* hwinfo: one-line summary of the hardware inventory from SYS_HW_INFO. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_hw_info h;
    if (hw_info(&h) != 0) {
        print("hwinfo: error\n");
        return 1;
    }
    printf("PCI functions : %u\n", h.pci_functions);
    printf("Devices       : %u\n", h.devices);
    printf("Block devices : %u\n", h.block_devices);
    printf("USB devices   : %u\n", h.usb_devices);
    printf("IRQ vectors   : %u\n", h.irq_vectors);
    printf("IRQ total     : %u\n", (unsigned)h.irq_total);
    printf("HW events     : %u\n", (unsigned)h.hw_events);
    return (h.pci_functions > 0) ? 0 : 1;
}
