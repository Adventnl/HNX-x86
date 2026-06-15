/* USB / HID / unified-input expansion self-tests.
 *
 * Runs at boot AFTER the USB/HID/input stack is initialised. Exercises the
 * xHCI ring + command-construction helpers, USB enumeration + descriptor
 * parsing, the HID boot-report parsers, and the unified input queue / source
 * tagging — preferring live devices where available and falling back to
 * honest synthetic-report unit tests otherwise. */
#ifndef MYOS_USB_EXT_TESTS_H
#define MYOS_USB_EXT_TESTS_H

void usb_ext_tests_run(void);

#endif /* MYOS_USB_EXT_TESTS_H */
