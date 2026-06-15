/* Generic driver/device lifecycle dispatch (see driver.h). Each entry point
 * updates the device's lifecycle state, invokes the matching/bound driver hook
 * (if any) and emits a hardware event so userland can observe the transition.
 *
 * Hooks are optional: a driver with no suspend/resume/reset still transitions
 * cleanly (the state machine is the contract; the hook is the side effect). */
#include "driver.h"
#include "driver_registry.h"
#include "device_power.h"
#include "hw_event_bus.h"
#include "log.h"

void device_set_state(struct device *dev, enum device_state state) {
    if (dev) {
        dev->state = state;
    }
}

const char *device_state_name(enum device_state state) {
    switch (state) {
    case DEVICE_STATE_DISCOVERED:  return "discovered";
    case DEVICE_STATE_MATCHED:     return "matched";
    case DEVICE_STATE_INITIALIZED: return "initialized";
    case DEVICE_STATE_ACTIVE:      return "active";
    case DEVICE_STATE_SUSPENDED:   return "suspended";
    case DEVICE_STATE_FAILED:      return "failed";
    case DEVICE_STATE_REMOVED:     return "removed";
    default:                       return "unknown";
    }
}

int driver_probe(struct device *dev) {
    if (!dev) {
        return DRIVER_PROBE_ERROR;
    }
    device_set_state(dev, DEVICE_STATE_DISCOVERED);
    for (struct driver *drv = driver_registry_head(); drv; drv = drv->next) {
        if (drv->type != dev->type || !drv->probe) {
            continue;
        }
        device_set_state(dev, DEVICE_STATE_MATCHED);
        int r = drv->probe(dev);
        if (r == 0) {
            dev->driver = drv;
            device_set_state(dev, DEVICE_STATE_ACTIVE);
            device_power_set(dev, DEV_POWER_D0);
            hw_event_emit(HW_EVENT_DRIVER_BOUND, (uint64_t)(uintptr_t)dev, 0, drv->name);
            return DRIVER_PROBE_OK;
        }
    }
    hw_event_emit(HW_EVENT_DRIVER_FAILED, (uint64_t)(uintptr_t)dev, 0, dev->name);
    return DRIVER_PROBE_NOMATCH;
}

int driver_remove(struct device *dev) {
    if (!dev) {
        return -1;
    }
    int r = 0;
    if (dev->driver && dev->driver->remove) {
        r = dev->driver->remove(dev);
    }
    dev->driver = NULL;
    device_power_set(dev, DEV_POWER_D3);
    device_set_state(dev, DEVICE_STATE_REMOVED);
    hw_event_emit(HW_EVENT_DEVICE_REMOVED, (uint64_t)(uintptr_t)dev, 0, dev->name);
    return r;
}

int driver_suspend(struct device *dev) {
    if (!dev) {
        return -1;
    }
    int r = 0;
    if (dev->driver && dev->driver->suspend) {
        r = dev->driver->suspend(dev);
    }
    device_power_set(dev, DEV_POWER_D3);
    device_set_state(dev, DEVICE_STATE_SUSPENDED);
    return r;
}

int driver_resume(struct device *dev) {
    if (!dev) {
        return -1;
    }
    device_power_set(dev, DEV_POWER_D0);
    int r = 0;
    if (dev->driver && dev->driver->resume) {
        r = dev->driver->resume(dev);
    }
    device_set_state(dev, DEVICE_STATE_ACTIVE);
    return r;
}

void driver_lifecycle_init(void) {
    kernel_log_ok("Driver lifecycle online");
}
