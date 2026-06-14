#include "device_id.h"

static const char *g_names[DEV_TYPE_COUNT] = {
    "bus", "pci", "block", "char", "input", "console", "storage",
};

const char *device_type_name(enum device_type t) {
    if ((unsigned)t < DEV_TYPE_COUNT) {
        return g_names[t];
    }
    return "unknown";
}
