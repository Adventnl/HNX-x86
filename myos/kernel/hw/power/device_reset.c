/* Device reset (see device_reset.h, driver.h). */
#include "device_reset.h"
#include "driver.h"
#include "device_power.h"

int device_reset_power_cycle(struct device *dev) {
    if (!dev) {
        return -1;
    }
    device_power_set(dev, DEV_POWER_D3);
    device_power_set(dev, DEV_POWER_D0);
    return 0;
}

int driver_reset(struct device *dev) {
    if (!dev) {
        return -1;
    }
    device_reset_power_cycle(dev);
    int r = 0;
    if (dev->driver && dev->driver->reset) {
        r = dev->driver->reset(dev);
    }
    /* A successful reset leaves the device active again. */
    device_set_state(dev, DEVICE_STATE_ACTIVE);
    return r;
}
