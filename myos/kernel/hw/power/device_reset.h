/* Device reset hooks. driver_reset() (declared in driver.h) is implemented here;
 * device_reset_power_cycle() is the low-level D3->D0 helper it builds on. */
#ifndef MYOS_DEVICE_RESET_H
#define MYOS_DEVICE_RESET_H

#include "types.h"

struct device;

/* Cycle the device power state down to D3 and back to D0. Returns 0. */
int device_reset_power_cycle(struct device *dev);

#endif /* MYOS_DEVICE_RESET_H */
