/* Driver core bring-up. */
#include "driver.h"
#include "driver_registry.h"
#include "string.h"
#include "log.h"

void device_init_struct(struct device *dev, const char *name, enum device_type type) {
    memset(dev, 0, sizeof(*dev));
    strlcpy(dev->name, name, sizeof(dev->name));
    dev->type = type;
    dev->id.vendor = 0xFFFF;
    dev->id.device = 0xFFFF;
}

void driver_core_init(void) {
    driver_registry_init();
    kernel_log_ok("Driver core online");
}
