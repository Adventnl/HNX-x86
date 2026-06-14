/* Device registry: a flat table of registered character devices, enumerated by
 * devfs and looked up by name. */
#ifndef MYOS_DEVICE_DEVICE_H
#define MYOS_DEVICE_DEVICE_H

#include "types.h"

struct char_device;

#define DEVICE_MAX_CHAR 16

/* Register the built-in null/zero devices. */
void device_init(void);

int device_register_char(struct char_device *cd);
struct char_device *device_find_char(const char *name);
int device_char_count(void);
struct char_device *device_char_at(int index);

#endif /* MYOS_DEVICE_DEVICE_H */
