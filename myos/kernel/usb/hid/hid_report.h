/* Minimal HID report-descriptor parser. MyOS drives boot-protocol devices (fixed
 * report layout), so this parser is used to *validate* the report descriptor and
 * extract the primary usage page + total input report size, not to build a full
 * field map. */
#ifndef MYOS_HID_REPORT_H
#define MYOS_HID_REPORT_H

#include "types.h"

/* Usage pages. */
#define HID_USAGE_PAGE_GENERIC  0x01
#define HID_USAGE_PAGE_KEYBOARD 0x07
#define HID_USAGE_PAGE_BUTTON   0x09

struct hid_report_info {
    uint16_t usage_page;     /* first global Usage Page    */
    uint16_t input_bits;     /* total Input report bits    */
    uint8_t  num_inputs;     /* number of Input main items */
    uint8_t  is_keyboard;
    uint8_t  is_mouse;
};

/* Walk a report descriptor; fill `out`. Returns 0 on success. */
int hid_report_parse(const uint8_t *desc, uint16_t len, struct hid_report_info *out);

#endif /* MYOS_HID_REPORT_H */
