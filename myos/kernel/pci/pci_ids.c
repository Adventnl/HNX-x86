/* Small static PCI class/vendor name tables (mirrors tools/pci/pci_ids_min.py).
 * A minimal hand-curated subset — not the full pci.ids database. */
#include "pci_ids.h"

const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
    case 0x00: return "unclassified";
    case 0x01:
        switch (subclass) {
        case 0x00: return "storage:scsi";
        case 0x01: return "storage:ide";
        case 0x06: return "storage:sata-ahci";
        case 0x08: return "storage:nvme";
        default:   return "storage";
        }
    case 0x02: return "network";
    case 0x03: return "display";
    case 0x04: return "multimedia";
    case 0x06:
        switch (subclass) {
        case 0x00: return "bridge:host";
        case 0x01: return "bridge:isa";
        case 0x04: return "bridge:pci";
        default:   return "bridge";
        }
    case 0x0C:
        switch (subclass) {
        case 0x03: return "serial:usb";
        default:   return "serial";
        }
    default: return "device";
    }
}

const char *pci_vendor_name(uint16_t vendor) {
    switch (vendor) {
    case 0x8086: return "Intel";
    case 0x1022: return "AMD";
    case 0x1234: return "QEMU/Bochs";
    case 0x1AF4: return "Red Hat/Virtio";
    case 0x1B36: return "Red Hat";
    case 0x10EC: return "Realtek";
    case 0x15AD: return "VMware";
    default:     return "unknown";
    }
}
