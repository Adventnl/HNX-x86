/* Driver core: a uniform driver/device model that the PCI, block, storage and
 * input layers register into. Generic enough to grow; specific bus matching
 * (e.g. PCI class/vendor) lives in the bus layers. */
#ifndef MYOS_DRIVER_H
#define MYOS_DRIVER_H

#include "types.h"
#include "device_id.h"

#define DEVICE_NAME_MAX 32

struct device;

struct driver {
    const char      *name;
    enum device_type type;
    /* Return 0 to claim the device, negative to decline. */
    int (*probe)(struct device *dev);
    struct driver   *next;       /* registry link */
};

struct device {
    char             name[DEVICE_NAME_MAX];
    enum device_type type;
    struct device_id id;
    void            *bus_data;   /* e.g. struct pci_device * */
    void            *drv_data;   /* driver-private state */
    struct driver   *driver;     /* bound driver, or NULL */
    struct device   *next;       /* registry link */
};

void driver_core_init(void);
void device_init_struct(struct device *dev, const char *name, enum device_type type);

#endif /* MYOS_DRIVER_H */
