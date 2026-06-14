/* AHCI HBA reset + port scan. */
#include "ahci_controller.h"
#include "ahci.h"
#include "ahci_port.h"
#include "ahci_disk.h"
#include "vmm.h"
#include "memory_layout.h"
#include "heap.h"
#include "log.h"

#define SPIN_LIMIT 10000000u

int ahci_controller_init(uint64_t abar) {
    if (vmm_map_mmio_2m(abar & ~LARGE_PAGE_MASK) != 0) {
        return 0;
    }
    struct hba_mem *hba = (struct hba_mem *)(uintptr_t)abar;

    /* Enable AHCI mode (skip the disruptive full HBA reset; QEMU presents the
     * ports ready once AE is set). */
    hba->ghc |= AHCI_GHC_AE;

    uint32_t pi = hba->pi;
    int disks = 0;
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) {
            continue;
        }
        if (!ahci_port_has_disk(&hba->ports[i])) {
            continue;
        }
        struct ahci_port port;
        if (ahci_port_init(&hba->ports[i], i, &port) != 0) {
            continue;
        }
        if (ahci_disk_register(&port) == 0) {
            kernel_log_ok("AHCI block device online");
            kernel_log_hex64("    ahci port      : ", (uint64_t)i);
            kernel_log_hex64("    sectors        : ", port.sector_count);
            disks++;
        }
    }
    return disks;
}
