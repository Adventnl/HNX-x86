/* Driver core: a uniform driver/device model that the PCI, block, storage and
 * input layers register into. Generic enough to grow; specific bus matching
 * (e.g. PCI class/vendor) lives in the bus layers.
 *
 * Prompt 6 extends the model with an explicit device lifecycle (discovered ->
 * matched -> active -> suspended/failed/removed), power states, and the
 * probe/remove/suspend/resume/reset hook set. */
#ifndef MYOS_DRIVER_H
#define MYOS_DRIVER_H

#include "types.h"
#include "device_id.h"
#include "device_power.h"

#define DEVICE_NAME_MAX 32

struct device;

/* Result of a driver probe() against a candidate device. */
enum driver_probe_result {
    DRIVER_PROBE_OK      = 0,    /* driver claimed the device          */
    DRIVER_PROBE_NOMATCH = -1,   /* not this driver's device           */
    DRIVER_PROBE_ERROR   = -2,   /* matched but failed to initialize   */
};

/* Device lifecycle states. */
enum device_state {
    DEVICE_STATE_DISCOVERED = 0,
    DEVICE_STATE_MATCHED,
    DEVICE_STATE_INITIALIZED,
    DEVICE_STATE_ACTIVE,
    DEVICE_STATE_SUSPENDED,
    DEVICE_STATE_FAILED,
    DEVICE_STATE_REMOVED,
};

struct driver {
    const char      *name;
    enum device_type type;
    /* Return 0 to claim the device, negative to decline. */
    int (*probe)(struct device *dev);
    int (*remove)(struct device *dev);
    int (*suspend)(struct device *dev);
    int (*resume)(struct device *dev);
    int (*reset)(struct device *dev);
    struct driver   *next;       /* registry link */
};

struct device {
    char                    name[DEVICE_NAME_MAX];
    enum device_type        type;
    struct device_id        id;
    void                   *bus_data;    /* e.g. struct pci_device * */
    void                   *drv_data;    /* driver-private state */
    struct driver          *driver;      /* bound driver, or NULL */
    enum device_state       state;       /* lifecycle state */
    enum device_power_state power_state; /* power state */
    struct device          *next;        /* registry link */
};

void driver_core_init(void);
void device_init_struct(struct device *dev, const char *name, enum device_type type);

/* ---- Lifecycle (Prompt 6) ------------------------------------------------- */
void        driver_lifecycle_init(void);

/* Generic lifecycle dispatchers: update device state, invoke the bound (or
 * matching) driver's hook, and emit hardware events. */
int         driver_probe(struct device *dev);
int         driver_remove(struct device *dev);
int         driver_suspend(struct device *dev);
int         driver_resume(struct device *dev);
int         driver_reset(struct device *dev);

void        device_set_state(struct device *dev, enum device_state state);
const char *device_state_name(enum device_state state);

#endif /* MYOS_DRIVER_H */
