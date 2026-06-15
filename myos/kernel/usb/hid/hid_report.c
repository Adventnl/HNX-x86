/* Minimal HID report-descriptor walker (see hid_report.h). */
#include "hid_report.h"
#include "string.h"

/* Short-item prefix: bTag[7:4] bType[3:2] bSize[1:0]. bSize 0..3 -> 0,1,2,4. */
#define ITEM_SIZE(prefix)  ((prefix) & 0x03)
#define ITEM_TYPE(prefix)  (((prefix) >> 2) & 0x03)
#define ITEM_TAG(prefix)   (((prefix) >> 4) & 0x0F)

#define TYPE_MAIN    0
#define TYPE_GLOBAL  1
#define TYPE_LOCAL   2

#define TAG_INPUT        0x8   /* main   */
#define TAG_USAGE_PAGE   0x0   /* global */
#define TAG_REPORT_SIZE  0x7   /* global */
#define TAG_REPORT_COUNT 0x9   /* global */
#define TAG_USAGE        0x0   /* local  */

static uint32_t item_data(const uint8_t *p, int size) {
    switch (size) {
    case 1:  return p[0];
    case 2:  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
    case 3:  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    default: return 0;
    }
}

int hid_report_parse(const uint8_t *desc, uint16_t len, struct hid_report_info *out) {
    if (!desc || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    uint32_t report_size = 0, report_count = 0;
    uint16_t off = 0;
    int have_page = 0;

    while (off < len) {
        uint8_t prefix = desc[off++];
        int size = ITEM_SIZE(prefix);
        int nbytes = (size == 3) ? 4 : size;
        if (off + nbytes > len) {
            break;
        }
        uint32_t data = item_data(desc + off, size);
        int type = ITEM_TYPE(prefix);
        int tag = ITEM_TAG(prefix);

        if (type == TYPE_GLOBAL && tag == TAG_USAGE_PAGE) {
            if (!have_page) {
                out->usage_page = (uint16_t)data;
                have_page = 1;
            }
            if (data == HID_USAGE_PAGE_KEYBOARD) {
                out->is_keyboard = 1;
            }
            if (data == HID_USAGE_PAGE_BUTTON) {
                out->is_mouse = 1;
            }
        } else if (type == TYPE_GLOBAL && tag == TAG_REPORT_SIZE) {
            report_size = data;
        } else if (type == TYPE_GLOBAL && tag == TAG_REPORT_COUNT) {
            report_count = data;
        } else if (type == TYPE_MAIN && tag == TAG_INPUT) {
            out->input_bits = (uint16_t)(out->input_bits + report_size * report_count);
            out->num_inputs++;
        }
        off = (uint16_t)(off + nbytes);
    }
    return 0;
}
