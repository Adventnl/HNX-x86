/* AHCI disk -> block_device. Transfers run with the kernel CR3 active so the
 * ABAR MMIO and DMA bounce buffer are reachable even when the call originates
 * from a user syscall (under a user CR3). The window is poll-only (no sleep). */
#include "ahci_disk.h"
#include "ahci_command.h"
#include "block_registry.h"
#include "block_device.h"
#include "user_copy.h"
#include "heap.h"
#include "string.h"

static int g_disk_index;

static int disk_rw(struct block_device *dev, uint64_t lba, void *buffer,
                   uint32_t count, int write) {
    struct ahci_port *port = (struct ahci_port *)dev->driver_data;
    uint8_t *p = (uint8_t *)buffer;
    uint64_t saved = user_with_kernel_cr3();
    int rc = 0;
    while (count > 0) {
        uint32_t chunk = (count > 8) ? 8 : count;
        if (write) {
            memcpy((void *)(uintptr_t)port->buffer_phys, p, (uint64_t)chunk * 512);
            if (ahci_command_rw(port, lba, chunk, 1) != 0) { rc = -1; break; }
        } else {
            if (ahci_command_rw(port, lba, chunk, 0) != 0) { rc = -1; break; }
            memcpy(p, (void *)(uintptr_t)port->buffer_phys, (uint64_t)chunk * 512);
        }
        p += (uint64_t)chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    user_restore_cr3(saved);
    return rc;
}

static int disk_read(struct block_device *dev, uint64_t lba, void *buffer, uint32_t count) {
    return disk_rw(dev, lba, buffer, count, 0);
}
static int disk_write(struct block_device *dev, uint64_t lba, const void *buffer, uint32_t count) {
    return disk_rw(dev, lba, (void *)buffer, count, 1);
}

int ahci_disk_register(struct ahci_port *port) {
    struct block_device *dev = (struct block_device *)kcalloc(1, sizeof(*dev));
    struct ahci_port *saved = (struct ahci_port *)kmalloc(sizeof(*saved));
    if (!dev || !saved) {
        return -1;
    }
    *saved = *port;
    dev->name[0] = 'd'; dev->name[1] = 'i'; dev->name[2] = 's'; dev->name[3] = 'k';
    dev->name[4] = (char)('0' + g_disk_index++);
    dev->name[5] = 0;
    dev->sector_count = saved->sector_count;
    dev->sector_size = 512;
    dev->driver_data = saved;
    dev->read = disk_read;
    dev->write = disk_write;
    return block_register_device(dev);
}
