/* Device identity + type taxonomy shared by the driver core and bus layers. */
#ifndef MYOS_DRIVER_DEVICE_ID_H
#define MYOS_DRIVER_DEVICE_ID_H

#include "types.h"

enum device_type {
    DEV_TYPE_BUS = 0,
    DEV_TYPE_PCI,
    DEV_TYPE_BLOCK,
    DEV_TYPE_CHAR,
    DEV_TYPE_INPUT,
    DEV_TYPE_CONSOLE,
    DEV_TYPE_STORAGE,
    DEV_TYPE_COUNT,
};

struct device_id {
    uint32_t vendor;        /* PCI vendor id (0xFFFF = any) */
    uint32_t device;        /* PCI device id (0xFFFF = any) */
    uint8_t  class_code;    /* PCI base class */
    uint8_t  subclass;
    uint8_t  prog_if;
};

const char *device_type_name(enum device_type t);

#endif /* MYOS_DRIVER_DEVICE_ID_H */
