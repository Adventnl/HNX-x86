/* msi_test: ring-3 check that MSI/MSI-X capability info is exposed. */
#include "stdio.h"
#include "unistd.h"

int main(void) {
    struct sys_msi_entry m[64];
    int n = msi_info(m, 64);
    if (n < 1) {
        print("[FAIL] msi_test\n");
        return 1;
    }
    int capable = 0;
    for (int i = 0; i < n; i++) {
        if (m[i].msi || m[i].msix) {
            capable++;
        }
    }
    if (capable < 1) {
        print("[FAIL] msi_test: no MSI/MSI-X capable functions\n");
        return 1;
    }
    print("[PASS] msi_test\n");
    return 0;
}
