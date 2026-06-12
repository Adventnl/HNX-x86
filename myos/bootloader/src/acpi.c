/* Stage 14: find the ACPI RSDP from the UEFI configuration table. */
#include "bootloader.h"

static EFI_GUID g_acpi20 = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID g_acpi10 = EFI_ACPI_10_TABLE_GUID;

static BOOLEAN guid_eq(const EFI_GUID *a, const EFI_GUID *b) {
    return (BOOLEAN)(memcmp(a, b, sizeof(EFI_GUID)) == 0);
}

EFI_STATUS bl_find_rsdp(myos_u64 *out_address) {
    myos_u64 acpi10_fallback = 0;

    for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *ct = &gST->ConfigurationTable[i];
        if (guid_eq(&ct->VendorGuid, &g_acpi20)) {
            *out_address = (myos_u64)(UINTN)ct->VendorTable;   /* prefer ACPI 2.0+ */
            return EFI_SUCCESS;
        }
        if (acpi10_fallback == 0 && guid_eq(&ct->VendorGuid, &g_acpi10)) {
            acpi10_fallback = (myos_u64)(UINTN)ct->VendorTable;
        }
    }

    if (acpi10_fallback != 0) {
        *out_address = acpi10_fallback;
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}
