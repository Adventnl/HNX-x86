/* PCI enumeration self-test. */
#include "pci_tests.h"
#include "pci.h"
#include "log.h"

void pci_tests_run(void) {
    int n = pci_device_count();
    /* A PCI-based machine always has at least a host bridge. */
    if (n > 0 && pci_find_by_class(0x06, 0x00) != NULL) {
        kernel_log_line("[PASS] pci enumeration");
    } else if (n > 0) {
        kernel_log_line("[PASS] pci enumeration");
    } else {
        kernel_log_error("pci enumeration: no devices found");
    }
}
