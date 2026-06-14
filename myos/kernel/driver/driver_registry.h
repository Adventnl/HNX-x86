/* Global driver + device registries and the generic probe loop. */
#ifndef MYOS_DRIVER_REGISTRY_H
#define MYOS_DRIVER_REGISTRY_H

#include "driver.h"

void driver_registry_init(void);

int driver_register(struct driver *driver);
int device_register(struct device *device);

/* Bind every still-unbound device to the first matching same-type driver whose
 * probe() succeeds. Returns the number of new bindings. */
int driver_probe_all(void);

void device_dump_all(void);
void driver_dump_all(void);

/* Iteration (used by the `devices` syscall/coreutil). */
int            device_count(void);
struct device *device_at(int index);

#endif /* MYOS_DRIVER_REGISTRY_H */
