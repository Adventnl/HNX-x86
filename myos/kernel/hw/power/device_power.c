/* Device power-state accounting (see device_power.h). */
#include "device_power.h"
#include "driver.h"

int device_power_set(struct device *dev, enum device_power_state state) {
    if (!dev) {
        return -1;
    }
    dev->power_state = state;
    return 0;
}

enum device_power_state device_power_get(struct device *dev) {
    if (!dev) {
        return DEV_POWER_D3;
    }
    return dev->power_state;
}

const char *device_power_state_name(enum device_power_state state) {
    switch (state) {
    case DEV_POWER_D0: return "D0-active";
    case DEV_POWER_D1: return "D1-light";
    case DEV_POWER_D2: return "D2-deep";
    case DEV_POWER_D3: return "D3-off";
    default:           return "unknown";
    }
}
