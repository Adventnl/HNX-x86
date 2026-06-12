/* ACPI table walk: RSDP -> XSDT (or RSDT) -> MADT ("APIC").
 *
 * All ACPI tables live in EfiACPIReclaimMemory / NVS, which is RAM-typed and
 * therefore inside the kernel identity map — plain pointer access is safe. */
#include "madt.h"
#include "kernel.h"
#include "log.h"

struct acpi_rsdp {
    char     signature[8];        /* "RSD PTR " */
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    /* ACPI 2.0+ fields: */
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
    /* variable-length entries follow */
} __attribute__((packed));

/* MADT entry types we understand. */
#define MADT_TYPE_LOCAL_APIC            0
#define MADT_TYPE_IO_APIC               1
#define MADT_TYPE_INTERRUPT_OVERRIDE    2
#define MADT_TYPE_LOCAL_APIC_NMI        4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE   5

static struct madt_info g_info;
static int g_parsed;

static int checksum_ok(const void *data, uint64_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint64_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum == 0;
}

static int sig_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static const struct acpi_madt *find_madt(uint64_t rsdp_address) {
    const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)(uintptr_t)rsdp_address;

    if (!sig_eq(rsdp->signature, "RSD PTR ", 8)) {
        return NULL;
    }
    if (!checksum_ok(rsdp, 20)) {          /* ACPI 1.0 part */
        return NULL;
    }

    int use_xsdt = 0;
    uint64_t sdt_addr = rsdp->rsdt_address;
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0 &&
        checksum_ok(rsdp, rsdp->length)) {
        use_xsdt = 1;
        sdt_addr = rsdp->xsdt_address;     /* XSDT preferred */
    }
    if (sdt_addr == 0) {
        return NULL;
    }

    const struct acpi_sdt_header *sdt =
        (const struct acpi_sdt_header *)(uintptr_t)sdt_addr;
    if (!checksum_ok(sdt, sdt->length)) {
        return NULL;
    }

    uint64_t entry_size = use_xsdt ? 8 : 4;
    uint64_t count = (sdt->length - sizeof(*sdt)) / entry_size;
    const uint8_t *entries = (const uint8_t *)(uintptr_t)(sdt_addr + sizeof(*sdt));

    for (uint64_t i = 0; i < count; i++) {
        uint64_t table_addr;
        if (use_xsdt) {
            uint64_t v;
            memcpy(&v, entries + i * 8, 8);   /* unaligned-safe */
            table_addr = v;
        } else {
            uint32_t v;
            memcpy(&v, entries + i * 4, 4);
            table_addr = v;
        }
        const struct acpi_sdt_header *t =
            (const struct acpi_sdt_header *)(uintptr_t)table_addr;
        if (sig_eq(t->signature, "APIC", 4) && checksum_ok(t, t->length)) {
            return (const struct acpi_madt *)t;
        }
    }
    return NULL;
}

int madt_init(uint64_t rsdp_address) {
    if (rsdp_address == 0) {
        return -1;
    }
    const struct acpi_madt *madt = find_madt(rsdp_address);
    if (!madt) {
        return -1;
    }

    g_info.local_apic_physical_base = madt->local_apic_address;

    const uint8_t *p   = (const uint8_t *)madt + sizeof(struct acpi_madt);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (p + 2 <= end) {
        uint8_t type = p[0];
        uint8_t len  = p[1];
        if (len < 2 || p + len > end) {
            break;                          /* malformed entry: stop */
        }
        switch (type) {
        case MADT_TYPE_LOCAL_APIC: {
            /* {type,len,acpi_cpu_id,apic_id,flags(4)} — count enabled CPUs */
            uint32_t flags;
            memcpy(&flags, p + 4, 4);
            if (flags & 1) {
                g_info.local_apic_count++;
            }
            break;
        }
        case MADT_TYPE_IO_APIC: {
            /* {type,len,id,reserved,address(4),gsi_base(4)} — record first */
            if (g_info.io_apic_physical_base == 0) {
                uint32_t addr, gsi;
                memcpy(&addr, p + 4, 4);
                memcpy(&gsi, p + 8, 4);
                g_info.io_apic_id = p[2];
                g_info.io_apic_physical_base = addr;
                g_info.global_system_interrupt_base = gsi;
            }
            break;
        }
        case MADT_TYPE_INTERRUPT_OVERRIDE: {
            /* {type,len,bus,source,gsi(4),flags(2)} */
            if (p[3] == 0) {                /* legacy IRQ0 (PIT) override */
                uint32_t gsi;
                uint16_t flags;
                memcpy(&gsi, p + 4, 4);
                memcpy(&flags, p + 8, 2);
                g_info.has_legacy_irq0_override = 1;
                g_info.irq0_gsi = gsi;
                g_info.irq0_flags = flags;
            }
            break;
        }
        case MADT_TYPE_LOCAL_APIC_OVERRIDE: {
            /* {type,len,reserved(2),address(8)} — 64-bit LAPIC base */
            uint64_t addr;
            memcpy(&addr, p + 4, 8);
            g_info.local_apic_physical_base = addr;
            break;
        }
        case MADT_TYPE_LOCAL_APIC_NMI:
        default:
            break;                          /* recognized or ignored */
        }
        p += len;
    }

    if (g_info.local_apic_physical_base == 0) {
        return -1;
    }
    g_parsed = 1;
    return 0;
}

const struct madt_info *madt_get_info(void) {
    return g_parsed ? &g_info : NULL;
}

void madt_dump_info(void) {
    kernel_log_hex64("    lapic base   : ", g_info.local_apic_physical_base);
    kernel_log_hex64("    cpu count    : ", g_info.local_apic_count);
    kernel_log_hex64("    ioapic base  : ", g_info.io_apic_physical_base);
    kernel_log_hex64("    ioapic id    : ", g_info.io_apic_id);
    kernel_log_hex64("    gsi base     : ", g_info.global_system_interrupt_base);
    if (g_info.has_legacy_irq0_override) {
        kernel_log_hex64("    irq0 -> gsi  : ", g_info.irq0_gsi);
    }
}
