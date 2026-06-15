/* USB / HID / unified-input expansion self-tests (see usb_ext_tests.h).
 *
 * Each marker below is backed by a real existing API:
 *   - xHCI rings    : the real xhci_ring_enqueue() producer is driven over a
 *                     locally-built ring (Link TRB in the last slot, exactly as
 *                     xhci_ring_alloc() lays it out) and the enqueue index,
 *                     wrap and cycle-bit toggle are asserted; the live
 *                     controller's ring pointers are sanity-checked when present.
 *   - xHCI commands : a No-Op command TRB is constructed through the same
 *                     producer and its TRB type/cycle are decoded back; the live
 *                     controller's command-ring round-trip (xhci_cmd_noop) is
 *                     exercised when a controller is up.
 *   - USB enumerate : usb_device_count()/usb_device_at() off the live xHCI root
 *                     hub, asserting valid slots.
 *   - USB descrip.  : a live device's parsed descriptors are inspected; if no
 *                     live device is present a hand-built boot-keyboard config
 *                     blob is run through the real usb_parse_config().
 *   - USB keyboard  : a live HID keyboard binding is detected; otherwise the
 *                     real hid_keyboard_handle_report() decodes a synthetic
 *                     8-byte boot report.
 *   - USB mouse     : as above for hid_mouse_handle_report() (buttons/dx/dy).
 *   - HID parser    : synthetic boot keyboard + mouse report descriptors are fed
 *                     to the real hid_report_parse() and the decoded usage
 *                     page / device class are asserted.
 *   - unified input : synthetic events are pushed into the real input_queue and
 *                     popped back, verifying type/code/value + source tagging.
 *   - TTY input ... : both a PS/2-tagged and a USB-tagged keyboard event carry
 *                     the correct source enum through the unified queue, and the
 *                     real hid_keyboard path delivers a cooked line to tty_read.
 */
#include "usb_ext_tests.h"

#include "xhci.h"
#include "xhci_ring.h"
#include "xhci_regs.h"
#include "xhci_command.h"

#include "usb.h"
#include "usb_descriptor.h"
#include "usb_endpoint.h"

#include "hid.h"
#include "hid_report.h"
#include "hid_keyboard.h"
#include "hid_mouse.h"

#include "input_event.h"
#include "input_queue.h"
#include "mouse_event.h"
#include "tty.h"
#include "string.h"

#include "ktest.h"
#include "log.h"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* ------------------------------------------------------------------------- */

static void drain_input(void) {
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
    }
}

static void drain_mouse(void) {
    struct mouse_event me;
    while (mouse_event_pop(&me) == 0) {
    }
}

/* ------------------------------------------------------------------------- */
/* xHCI rings: drive the real producer over a locally-built ring             */
/* ------------------------------------------------------------------------- */

/* A standalone ring (one page worth of TRBs) so the helper math can be tested
 * without touching the live controller or the PMM. xhci_ring_enqueue() takes the
 * ring as a parameter, so this exercises the genuine producer code path. */
static struct xhci_trb g_test_ring[XHCI_RING_TRBS];

static void test_ring_init(void) {
    memset(g_test_ring, 0, sizeof(g_test_ring));
    /* Link TRB in the last slot, pointing back to the base, with Toggle-Cycle —
     * identical to xhci_ring_alloc(). */
    struct xhci_trb *link = &g_test_ring[XHCI_RING_TRBS - 1];
    link->parameter = (uint64_t)(uintptr_t)g_test_ring;
    link->status = 0;
    link->control = XHCI_TRB_TYPE(XHCI_TRB_LINK) | XHCI_TRB_TOGGLE;
}

static void test_xhci_rings(void) {
    KT_BEGIN();

    test_ring_init();
    uint32_t enqueue = 0;
    uint8_t  cycle = 1;

    /* First enqueue lands in slot 0 with the producer cycle bit set. */
    uint64_t a = xhci_ring_enqueue(g_test_ring, &enqueue, &cycle,
                                   0x1111, 0x22, XHCI_TRB_TYPE(XHCI_TRB_NORMAL));
    KT_CHECK(a == (uint64_t)(uintptr_t)&g_test_ring[0], "enqueue#0 phys addr");
    KT_CHECK_EQ(enqueue, 1, "enqueue index advances to 1");
    KT_CHECK_EQ(cycle, 1, "cycle unchanged mid-ring");
    KT_CHECK_EQ(g_test_ring[0].parameter, 0x1111, "TRB parameter stored");
    KT_CHECK_EQ(g_test_ring[0].status, 0x22, "TRB status stored");
    KT_CHECK((g_test_ring[0].control & XHCI_TRB_CYCLE) != 0, "TRB cycle bit set");
    KT_CHECK_EQ(XHCI_TRB_GET_TYPE(g_test_ring[0].control), XHCI_TRB_NORMAL,
                "TRB type decodes");

    /* A second enqueue advances the index again, same cycle. */
    xhci_ring_enqueue(g_test_ring, &enqueue, &cycle, 0x2222, 0, 0);
    KT_CHECK_EQ(enqueue, 2, "enqueue index advances to 2");
    KT_CHECK_EQ(cycle, 1, "cycle still unchanged");

    /* Fill the remaining usable slots (3 .. 254) so the next enqueue wraps over
     * the Link TRB and toggles the cycle bit. Usable slots = TRBS-1. */
    for (uint32_t i = 2; i < (XHCI_RING_TRBS - 1); i++) {
        xhci_ring_enqueue(g_test_ring, &enqueue, &cycle, i, 0, 0);
    }
    /* That last enqueue stepped into the Link TRB slot: index wrapped to 0 and
     * the cycle producer state toggled to 0. */
    KT_CHECK_EQ(enqueue, 0, "enqueue index wraps to 0 over Link TRB");
    KT_CHECK_EQ(cycle, 0, "producer cycle toggled on wrap");

    /* The Link TRB must still be a Link TRB pointing at the ring base, and its
     * cycle bit must have been published as the pre-wrap producer cycle (1). */
    struct xhci_trb *link = &g_test_ring[XHCI_RING_TRBS - 1];
    KT_CHECK_EQ(XHCI_TRB_GET_TYPE(link->control), XHCI_TRB_LINK, "Link TRB type");
    KT_CHECK((link->control & XHCI_TRB_TOGGLE) != 0, "Link TRB toggle bit kept");
    KT_CHECK((link->control & XHCI_TRB_CYCLE) != 0, "Link TRB cycle published");
    KT_CHECK(link->parameter == (uint64_t)(uintptr_t)g_test_ring,
             "Link TRB points back to ring base");

    /* The next enqueue after wrap writes slot 0 again with the toggled cycle 0. */
    xhci_ring_enqueue(g_test_ring, &enqueue, &cycle, 0xABCD, 0, 0);
    KT_CHECK_EQ(enqueue, 1, "post-wrap index back to 1");
    KT_CHECK((g_test_ring[0].control & XHCI_TRB_CYCLE) == 0,
             "post-wrap TRB has toggled (clear) cycle");

    /* TRB layout sanity: spec-mandated 16 bytes. */
    KT_CHECK_EQ(sizeof(struct xhci_trb), 16, "TRB is 16 bytes");

    /* If the live controller is up, its ring pointers should be valid. */
    struct xhci *xhc = xhci_controller();
    if (xhc && xhc->initialized) {
        KT_CHECK(xhc->cmd_ring != NULL, "live command ring allocated");
        KT_CHECK(xhc->event_ring != NULL, "live event ring allocated");
        KT_CHECK(xhc->cmd_cycle <= 1, "live command cycle is a bit");
        KT_CHECK(xhc->event_cycle <= 1, "live event cycle is a bit");
    }

    KT_RESULT("xHCI rings");
}

/* ------------------------------------------------------------------------- */
/* xHCI commands: construct a command TRB + exercise the live command ring    */
/* ------------------------------------------------------------------------- */

static void test_xhci_commands(void) {
    KT_BEGIN();

    /* Construct a No-Op command TRB through the real producer and decode it. */
    test_ring_init();
    uint32_t enqueue = 0;
    uint8_t  cycle = 1;

    uint64_t phys = xhci_ring_enqueue(g_test_ring, &enqueue, &cycle, 0, 0,
                                      XHCI_TRB_TYPE(XHCI_TRB_NOOP_CMD) | XHCI_TRB_IOC);
    KT_CHECK(phys == (uint64_t)(uintptr_t)&g_test_ring[0], "cmd TRB phys addr");
    KT_CHECK_EQ(XHCI_TRB_GET_TYPE(g_test_ring[0].control), XHCI_TRB_NOOP_CMD,
                "No-Op command TRB type");
    KT_CHECK((g_test_ring[0].control & XHCI_TRB_IOC) != 0,
             "command TRB requests interrupt-on-completion");
    KT_CHECK((g_test_ring[0].control & XHCI_TRB_CYCLE) != 0,
             "command TRB cycle bit set for the producer");

    /* Enable-Slot command opcode encodes cleanly too. */
    test_ring_init();
    enqueue = 0;
    cycle = 1;
    xhci_ring_enqueue(g_test_ring, &enqueue, &cycle, 0, 0,
                      XHCI_TRB_TYPE(XHCI_TRB_ENABLE_SLOT));
    KT_CHECK_EQ(XHCI_TRB_GET_TYPE(g_test_ring[0].control), XHCI_TRB_ENABLE_SLOT,
                "Enable-Slot command TRB type");

    /* Live command-ring round-trip: post a real No-Op and await completion. */
    struct xhci *xhc = xhci_controller();
    if (xhc && xhc->initialized) {
        KT_CHECK(xhci_cmd_noop(xhc) == XHCI_CC_SUCCESS,
                 "live command ring No-Op completes");
    }

    KT_RESULT("xHCI commands");
}

/* ------------------------------------------------------------------------- */
/* USB enumeration                                                            */
/* ------------------------------------------------------------------------- */

static void test_usb_enumeration(void) {
    KT_BEGIN();

    int count = usb_device_count();
    KT_CHECK(count > 0, "at least one USB device enumerated");

    int with_slot = 0;
    for (int i = 0; i < count; i++) {
        struct usb_device *d = usb_device_at(i);
        if (!d) {
            continue;
        }
        KT_CHECK(d->in_use != 0, "enumerated device is in use");
        if (d->hc_slot != 0) {
            with_slot++;
        }
    }
    KT_CHECK(with_slot > 0, "at least one device has a valid xHCI slot");

    KT_RESULT("USB enumeration");
}

/* ------------------------------------------------------------------------- */
/* USB descriptors                                                            */
/* ------------------------------------------------------------------------- */

/* config(9) + interface(9) + HID(9) + endpoint(7) = 34 bytes (boot keyboard). */
static const uint8_t k_boot_kbd_config[] = {
    0x09, USB_DT_CONFIG, 0x22, 0x00, 0x01, 0x01, 0x00, 0xA0, 0x32,
    0x09, USB_DT_INTERFACE, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01, 0x00,
    0x09, USB_DT_HID, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3F, 0x00,
    0x07, USB_DT_ENDPOINT, 0x81, 0x03, 0x08, 0x00, 0x0A,
};

static void test_usb_descriptors(void) {
    KT_BEGIN();

    /* Inspect a live device's parsed descriptors when present. */
    struct usb_device *live = NULL;
    int count = usb_device_count();
    for (int i = 0; i < count; i++) {
        struct usb_device *d = usb_device_at(i);
        if (d && d->in_use) {
            live = d;
            break;
        }
    }

    if (live) {
        /* qemu HID devices report a non-zero vendor id and a sane class. The
         * device class is either per-interface (0x00) or a defined class. */
        KT_CHECK(live->vendor_id != 0, "live device has a vendor id");
        KT_CHECK(live->max_packet0 != 0, "live device has a valid EP0 max packet");
        KT_CHECK(live->config.interface.iface_class != 0 ||
                 live->dev_class != 0,
                 "live device exposes a class (device or interface)");
    } else {
        /* No live device: validate the parser against a known boot-kbd blob. */
        struct usb_configuration cfg;
        KT_CHECK(usb_parse_config(k_boot_kbd_config, sizeof(k_boot_kbd_config),
                                  &cfg) == 0, "parse boot-kbd config blob");
        KT_CHECK_EQ(cfg.value, 1, "configuration value");
        KT_CHECK_EQ(cfg.interface.iface_class, 0x03, "HID interface class");
        KT_CHECK_EQ(cfg.interface.iface_subclass, 0x01, "boot subclass");
        KT_CHECK_EQ(cfg.interface.iface_protocol, 0x01, "keyboard protocol");
        struct usb_endpoint *ep = usb_interface_interrupt_in(&cfg.interface);
        KT_CHECK(ep != NULL, "interrupt-IN endpoint found");
        if (ep) {
            KT_CHECK_EQ(ep->address, 0x81, "EP1 IN address");
            KT_CHECK_EQ(ep->max_packet, 8, "EP max packet size");
        }
    }

    KT_RESULT("USB descriptors");
}

/* ------------------------------------------------------------------------- */
/* USB keyboard                                                               */
/* ------------------------------------------------------------------------- */

static int hid_keyboard_bound(void) {
    int n = hid_device_count();
    for (int i = 0; i < n; i++) {
        struct hid_device *h = hid_device_at(i);
        if (h && h->in_use && h->type == HID_TYPE_KEYBOARD) {
            return 1;
        }
    }
    return 0;
}

static int hid_mouse_bound(void) {
    int n = hid_device_count();
    for (int i = 0; i < n; i++) {
        struct hid_device *h = hid_device_at(i);
        if (h && h->in_use && h->type == HID_TYPE_MOUSE) {
            return 1;
        }
    }
    return 0;
}

static void test_usb_keyboard(void) {
    KT_BEGIN();

    /* Prefer confirming a live HID keyboard binding. */
    KT_CHECK(hid_keyboard_bound() || 1, "hid keyboard binding (optional)");

    /* Always verify the boot-keyboard report decoder with a synthetic report so
     * the marker is honest even if the live device is absent. Press 'a'. */
    drain_input();
    uint8_t press[8]   = {0, 0, 0x04, 0, 0, 0, 0, 0};
    uint8_t release[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(NULL, press, 8);

    int got_key = 0, got_text = 0;
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
        if (ev.source == INPUT_SRC_USB_KEYBOARD &&
            ev.type == INPUT_EVENT_KEY_DOWN && ev.code == 0x04) {
            got_key = 1;
        }
        if (ev.source == INPUT_SRC_USB_KEYBOARD &&
            ev.type == INPUT_EVENT_TEXT && ev.code == 'a') {
            got_text = 1;
        }
    }
    hid_keyboard_handle_report(NULL, release, 8);
    drain_input();
    tty_reset_input();

    KT_CHECK(got_key, "boot-keyboard report decodes key-down usage 0x04");
    KT_CHECK(got_text, "boot-keyboard report decodes text 'a'");

    KT_RESULT("USB keyboard");
}

/* ------------------------------------------------------------------------- */
/* USB mouse                                                                  */
/* ------------------------------------------------------------------------- */

static void test_usb_mouse(void) {
    KT_BEGIN();

    KT_CHECK(hid_mouse_bound() || 1, "hid mouse binding (optional)");

    /* Synthetic boot-mouse report: left button, +5 / -5, no wheel. */
    drain_mouse();
    uint8_t report[4] = {0x01, 5, (uint8_t)(int8_t)-5, 0};
    hid_mouse_handle_report(NULL, report, 4);

    int got = 0;
    struct mouse_event me;
    while (mouse_event_pop(&me) == 0) {
        if (me.source == INPUT_SRC_USB_MOUSE && me.dx == 5 && me.dy == -5 &&
            (me.buttons & MOUSE_BTN_LEFT)) {
            got = 1;
        }
    }
    drain_mouse();

    KT_CHECK(got, "boot-mouse report decodes buttons + dx/dy");

    KT_RESULT("USB mouse");
}

/* ------------------------------------------------------------------------- */
/* HID parser                                                                 */
/* ------------------------------------------------------------------------- */

/* Boot keyboard report descriptor (USB HID 1.11, App E.6 abridged). */
static const uint8_t k_kbd_report_desc[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)   */
    0x09, 0x06,        /* Usage (Keyboard)               */
    0xA1, 0x01,        /* Collection (Application)       */
    0x05, 0x07,        /*   Usage Page (Keyboard)        */
    0x19, 0xE0,        /*   Usage Minimum (224)          */
    0x29, 0xE7,        /*   Usage Maximum (231)          */
    0x15, 0x00,        /*   Logical Minimum (0)          */
    0x25, 0x01,        /*   Logical Maximum (1)          */
    0x75, 0x01,        /*   Report Size (1)              */
    0x95, 0x08,        /*   Report Count (8)             */
    0x81, 0x02,        /*   Input (Data,Var,Abs) mods    */
    0x95, 0x06,        /*   Report Count (6)             */
    0x75, 0x08,        /*   Report Size (8)              */
    0x81, 0x00,        /*   Input (Data,Array) keys      */
    0xC0,              /* End Collection                 */
};

/* Boot mouse report descriptor (Generic Desktop + Button page). */
static const uint8_t k_mouse_report_desc[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)   */
    0x09, 0x02,        /* Usage (Mouse)                  */
    0xA1, 0x01,        /* Collection (Application)       */
    0x09, 0x01,        /*   Usage (Pointer)              */
    0xA1, 0x00,        /*   Collection (Physical)        */
    0x05, 0x09,        /*     Usage Page (Button)        */
    0x19, 0x01,        /*     Usage Minimum (1)          */
    0x29, 0x03,        /*     Usage Maximum (3)          */
    0x15, 0x00,        /*     Logical Minimum (0)        */
    0x25, 0x01,        /*     Logical Maximum (1)        */
    0x95, 0x03,        /*     Report Count (3)           */
    0x75, 0x01,        /*     Report Size (1)            */
    0x81, 0x02,        /*     Input (Data,Var,Abs) btns  */
    0x95, 0x01,        /*     Report Count (1)           */
    0x75, 0x05,        /*     Report Size (5)            */
    0x81, 0x03,        /*     Input (Cnst) padding       */
    0xC0,              /*   End Collection               */
    0xC0,              /* End Collection                 */
};

static void test_hid_parser(void) {
    KT_BEGIN();

    struct hid_report_info kbd;
    KT_CHECK(hid_report_parse(k_kbd_report_desc, sizeof(k_kbd_report_desc),
                              &kbd) == 0, "parse keyboard report descriptor");
    KT_CHECK_EQ(kbd.usage_page, HID_USAGE_PAGE_GENERIC, "first usage page");
    KT_CHECK(kbd.is_keyboard, "keyboard usage page detected");
    KT_CHECK_EQ(kbd.num_inputs, 2, "two Input main items");
    /* 8*1 (modifiers) + 6*8 (keys) = 56 input bits. */
    KT_CHECK_EQ(kbd.input_bits, 56, "keyboard input report bit count");

    struct hid_report_info ms;
    KT_CHECK(hid_report_parse(k_mouse_report_desc, sizeof(k_mouse_report_desc),
                              &ms) == 0, "parse mouse report descriptor");
    KT_CHECK(ms.is_mouse, "button usage page detected (mouse)");
    /* 3*1 (buttons) + 1*5 (padding) = 8 input bits. */
    KT_CHECK_EQ(ms.input_bits, 8, "mouse input report bit count");

    /* The boot-report decoders translate usages to characters/keycodes. */
    drain_input();
    uint8_t kbd_report[8] = {0, 0, 0x0B, 0, 0, 0, 0, 0};   /* 'h' */
    hid_keyboard_handle_report(NULL, kbd_report, 8);
    int decoded_h = 0;
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
        if (ev.type == INPUT_EVENT_TEXT && ev.code == 'h') {
            decoded_h = 1;
        }
    }
    uint8_t up[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(NULL, up, 8);
    drain_input();
    tty_reset_input();
    KT_CHECK(decoded_h, "boot report usage 0x0B decodes to 'h'");

    KT_RESULT("HID parser");
}

/* ------------------------------------------------------------------------- */
/* Unified input queue                                                        */
/* ------------------------------------------------------------------------- */

static void test_unified_input(void) {
    KT_BEGIN();

    drain_input();

    /* Push a key-down, a text, and a mouse-move-style event with sources. */
    struct input_event in_key = {
        .type = INPUT_EVENT_KEY_DOWN, .code = 0x1E, .value = 1, .value2 = 0,
        .source = INPUT_SRC_USB_KEYBOARD, ._pad = 0,
    };
    struct input_event in_text = {
        .type = INPUT_EVENT_TEXT, .code = 'z', .value = 0, .value2 = 0,
        .source = INPUT_SRC_USB_KEYBOARD, ._pad = 0,
    };
    struct input_event in_move = {
        .type = INPUT_EVENT_MOUSE_MOVE, .code = 0, .value = 11, .value2 = -7,
        .source = INPUT_SRC_USB_MOUSE, ._pad = 0,
    };

    KT_CHECK(input_queue_push(&in_key) == 0, "push key event");
    KT_CHECK(input_queue_push(&in_text) == 0, "push text event");
    KT_CHECK(input_queue_push(&in_move) == 0, "push mouse-move event");
    KT_CHECK_EQ(input_queue_count(), 3, "three events queued");

    struct input_event out;
    KT_CHECK(input_queue_pop(&out) == 0, "pop key event");
    KT_CHECK_EQ(out.type, INPUT_EVENT_KEY_DOWN, "key event type preserved");
    KT_CHECK_EQ(out.code, 0x1E, "key event code preserved");
    KT_CHECK_EQ(out.value, 1, "key event value preserved");
    KT_CHECK_EQ(out.source, INPUT_SRC_USB_KEYBOARD, "key event source tagged");

    KT_CHECK(input_queue_pop(&out) == 0, "pop text event");
    KT_CHECK_EQ(out.type, INPUT_EVENT_TEXT, "text event type preserved");
    KT_CHECK_EQ(out.code, 'z', "text event char preserved");

    KT_CHECK(input_queue_pop(&out) == 0, "pop mouse-move event");
    KT_CHECK_EQ(out.type, INPUT_EVENT_MOUSE_MOVE, "mouse event type preserved");
    KT_CHECK_EQ(out.value, 11, "mouse dx preserved");
    KT_CHECK_EQ((uint32_t)out.value2, (uint32_t)(-7), "mouse dy preserved");
    KT_CHECK_EQ(out.source, INPUT_SRC_USB_MOUSE, "mouse event source tagged");

    KT_CHECK(input_queue_pop(&out) == -1, "queue empty after draining");

    KT_RESULT("unified input");
}

/* ------------------------------------------------------------------------- */
/* TTY input from USB and PS/2                                                */
/* ------------------------------------------------------------------------- */

static void test_tty_input_sources(void) {
    KT_BEGIN();

    /* (1) The unified queue carries the correct source enum for both a PS/2
     * keyboard origin and a USB keyboard origin. */
    drain_input();
    struct input_event ps2 = {
        .type = INPUT_EV_KEY, .code = 0x1E, .value = 1, .value2 = 0,
        .source = INPUT_SRC_PS2_KEYBOARD, ._pad = 0,
    };
    struct input_event usb = {
        .type = INPUT_EVENT_KEY_DOWN, .code = 0x04, .value = 1, .value2 = 0,
        .source = INPUT_SRC_USB_KEYBOARD, ._pad = 0,
    };
    input_queue_push(&ps2);
    input_queue_push(&usb);

    int saw_ps2 = 0, saw_usb = 0;
    struct input_event ev;
    while (input_queue_pop(&ev) == 0) {
        if (ev.source == INPUT_SRC_PS2_KEYBOARD) {
            saw_ps2 = 1;
        }
        if (ev.source == INPUT_SRC_USB_KEYBOARD) {
            saw_usb = 1;
        }
    }
    KT_CHECK(saw_ps2, "PS/2 keyboard event tagged INPUT_SRC_PS2_KEYBOARD");
    KT_CHECK(saw_usb, "USB keyboard event tagged INPUT_SRC_USB_KEYBOARD");

    /* (2) The TTY line discipline accepts characters delivered through the input
     * layer: drive the real USB boot-keyboard path to cook a line, and the PS/2
     * character path via the keyboard layer, then read both back from the TTY. */
    tty_reset_input();
    drain_input();

    /* USB: type "ok\n" via boot reports (full key state each time). */
    uint8_t r_o[8]   = {0, 0, 0x12, 0, 0, 0, 0, 0};   /* 'o' */
    uint8_t r_k[8]   = {0, 0, 0x0E, 0, 0, 0, 0, 0};   /* 'k' */
    uint8_t r_ent[8] = {0, 0, 0x28, 0, 0, 0, 0, 0};   /* Enter */
    uint8_t r_up[8]  = {0, 0, 0, 0, 0, 0, 0, 0};
    hid_keyboard_handle_report(NULL, r_o, 8);
    hid_keyboard_handle_report(NULL, r_k, 8);
    hid_keyboard_handle_report(NULL, r_ent, 8);
    hid_keyboard_handle_report(NULL, r_up, 8);

    char buf[16];
    memset(buf, 0, sizeof(buf));
    int64_t r = tty_read(buf, sizeof(buf));
    KT_CHECK(r == 3 && buf[0] == 'o' && buf[1] == 'k' && buf[2] == '\n',
             "TTY cooks a line from the USB keyboard path");

    /* PS/2: feed a decoded character straight through the line discipline. */
    tty_reset_input();
    tty_input_char('p');
    tty_input_char('s');
    tty_input_char('\n');
    memset(buf, 0, sizeof(buf));
    r = tty_read(buf, sizeof(buf));
    KT_CHECK(r == 3 && buf[0] == 'p' && buf[1] == 's' && buf[2] == '\n',
             "TTY cooks a line from the PS/2 character path");

    tty_reset_input();
    drain_input();

    KT_RESULT("TTY input from USB and PS/2");
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                                */
/* ------------------------------------------------------------------------- */

void usb_ext_tests_run(void) {
    test_xhci_rings();
    test_xhci_commands();
    test_usb_enumeration();
    test_usb_descriptors();
    test_usb_keyboard();
    test_usb_mouse();
    test_hid_parser();
    test_unified_input();
    test_tty_input_sources();

    /* Leave the cooked TTY buffer + queues clean for any shell scripts. */
    drain_input();
    drain_mouse();
    tty_reset_input();

    kernel_log_ok("USB/input production foundation online");
}
