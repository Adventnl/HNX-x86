/* Hardware event records. The kernel emits these on device/driver/input
 * lifecycle changes; the event bus stores a recent ring and a running total. */
#ifndef MYOS_HW_EVENT_H
#define MYOS_HW_EVENT_H

#include "types.h"

enum hw_event_type {
    HW_EVENT_DEVICE_ADDED,
    HW_EVENT_DEVICE_REMOVED,
    HW_EVENT_DRIVER_BOUND,
    HW_EVENT_DRIVER_FAILED,
    HW_EVENT_INPUT,
    HW_EVENT_USB_DEVICE_ATTACHED,
    HW_EVENT_USB_DEVICE_REMOVED,
    HW_EVENT_TYPE_COUNT,
};

#define HW_EVENT_MSG_MAX 48

struct hw_event {
    uint32_t type;                       /* enum hw_event_type      */
    uint32_t seq;                        /* monotonic sequence id   */
    uint64_t a;                          /* type-specific operand   */
    uint64_t b;                          /* type-specific operand   */
    char     message[HW_EVENT_MSG_MAX];
};

const char *hw_event_type_name(enum hw_event_type type);

#endif /* MYOS_HW_EVENT_H */
