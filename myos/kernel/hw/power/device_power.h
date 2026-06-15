/* Device power-state model (ACPI-style D-states). The driver lifecycle drives
 * transitions; individual bus drivers may override device_power hooks later. */
#ifndef MYOS_DEVICE_POWER_H
#define MYOS_DEVICE_POWER_H

#include "types.h"

struct device;

enum device_power_state {
    DEV_POWER_D0 = 0,   /* fully powered / active        */
    DEV_POWER_D1,       /* light sleep                   */
    DEV_POWER_D2,       /* deeper sleep                  */
    DEV_POWER_D3,       /* off / suspended               */
};

int                     device_power_set(struct device *dev, enum device_power_state state);
enum device_power_state device_power_get(struct device *dev);
const char             *device_power_state_name(enum device_power_state state);

#endif /* MYOS_DEVICE_POWER_H */
