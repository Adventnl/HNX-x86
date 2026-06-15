/* msiinfo: per-PCI-function MSI / MSI-X capability summary. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_msi_entry m[64];
    int n = msi_info(m, 64);
    if (n < 0) {
        print("msiinfo: error\n");
        return 1;
    }
    printf("%-8s %-10s %-5s %-5s %s\n", "DEVICE", "ID", "MSI", "MSIX", "VECTORS");
    for (int i = 0; i < n; i++) {
        printf("%-8s %x:%x   %-5s %-5s %u\n", m[i].name, m[i].vendor, m[i].device,
               m[i].msi ? "yes" : "no", m[i].msix ? "yes" : "no", m[i].msix_count);
    }
    return 0;
}
