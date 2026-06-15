/* Hardware event type names (see hw_event.h). */
#include "hw_event.h"

const char *hw_event_type_name(enum hw_event_type type) {
    switch (type) {
    case HW_EVENT_DEVICE_ADDED:         return "device-added";
    case HW_EVENT_DEVICE_REMOVED:       return "device-removed";
    case HW_EVENT_DRIVER_BOUND:         return "driver-bound";
    case HW_EVENT_DRIVER_FAILED:        return "driver-failed";
    case HW_EVENT_INPUT:                return "input";
    case HW_EVENT_USB_DEVICE_ATTACHED:  return "usb-attached";
    case HW_EVENT_USB_DEVICE_REMOVED:   return "usb-removed";
    default:                            return "unknown";
    }
}
