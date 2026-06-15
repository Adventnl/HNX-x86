/* dmesg: print kernel diagnostics.
 *
 * MyOS exposes no kernel message ring buffer to ring-3, so this reports the
 * kernel's hardware/interrupt summary (the hw_info syscall) as a diagnostic
 * snapshot rather than a boot log. */
#include "stdio.h"
#include "unistd.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct sys_hw_info hw;
    if (hw_info(&hw) < 0) {
        eprint("dmesg: kernel diagnostics unavailable\n");
        return 1;
    }
    printf("[ kernel ] HNX MyOS x86_64\n");
    printf("[ pci    ] %u PCI functions\n", hw.pci_functions);
    printf("[ device ] %u devices, %u block, %u USB\n",
           hw.devices, hw.block_devices, hw.usb_devices);
    printf("[ irq    ] %u vectors, %llu interrupts delivered\n",
           hw.irq_vectors, hw.irq_total);
    printf("[ events ] %llu hardware events\n", hw.hw_events);
    return 0;
}
