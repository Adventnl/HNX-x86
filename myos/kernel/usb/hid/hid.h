/* USB HID core: matches HID-class interfaces, switches them to boot protocol,
 * configures the interrupt-IN endpoint, and dispatches incoming reports to the
 * keyboard/mouse handlers. */
#ifndef MYOS_HID_H
#define MYOS_HID_H

#include "types.h"
#include "usb.h"

#define HID_TYPE_KEYBOARD 1
#define HID_TYPE_MOUSE    2

/* HID class requests (bRequest, class/interface). */
#define HID_REQ_GET_REPORT   0x01
#define HID_REQ_SET_IDLE     0x0A
#define HID_REQ_SET_PROTOCOL 0x0B

struct hid_device {
    struct usb_device *usb;
    uint8_t  type;           /* HID_TYPE_*                  */
    uint8_t  intr_ep;        /* interrupt-IN endpoint addr  */
    uint16_t intr_mps;
    uint8_t  interval;
    uint64_t report_buf;     /* identity-mapped DMA buffer  */
    int      in_use;
};

void hid_init(void);             /* logs "[OK] USB HID core online" */
void hid_poll(void);             /* best-effort interrupt-report poll */

int                hid_device_count(void);
struct hid_device *hid_device_at(int index);

#endif /* MYOS_HID_H */
