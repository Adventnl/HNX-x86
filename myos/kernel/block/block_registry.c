/* Block device registry. */
#include "block_registry.h"
#include "block_cache.h"
#include "driver.h"
#include "driver_registry.h"
#include "heap.h"
#include "string.h"
#include "log.h"

static struct block_device *g_head;
static struct block_device *g_tail;
static int g_count;

void block_init(void) {
    g_head = g_tail = NULL;
    g_count = 0;
    block_cache_init();
    kernel_log_ok("Block layer online");
}

int block_register_device(struct block_device *device) {
    if (!device) {
        return -1;
    }
    if (device->sector_size == 0) {
        device->sector_size = BLOCK_SECTOR_SIZE;
    }
    device->next = NULL;
    if (g_tail) {
        g_tail->next = device;
    } else {
        g_head = device;
    }
    g_tail = device;
    g_count++;

    /* Mirror into the driver-core registry so `devices` can enumerate it. */
    struct device *d = (struct device *)kcalloc(1, sizeof(*d));
    if (d) {
        device_init_struct(d, device->name, DEV_TYPE_BLOCK);
        d->bus_data = device;
        device_register(d);
    }

    kernel_log("    block device: ");
    kernel_log(device->name);
    kernel_log_hex64("  sectors=", device->sector_count);
    return 0;
}

struct block_device *block_get_device(const char *name) {
    for (struct block_device *d = g_head; d; d = d->next) {
        if (strcmp(d->name, name) == 0) {
            return d;
        }
    }
    return NULL;
}

void block_dump_devices(void) {
    for (struct block_device *d = g_head; d; d = d->next) {
        kernel_log("    ");
        kernel_log(d->name);
        kernel_log_hex64("  sectors=", d->sector_count);
    }
}

int block_device_count(void) {
    return g_count;
}

struct block_device *block_device_at(int index) {
    int i = 0;
    for (struct block_device *d = g_head; d; d = d->next, i++) {
        if (i == index) {
            return d;
        }
    }
    return NULL;
}
