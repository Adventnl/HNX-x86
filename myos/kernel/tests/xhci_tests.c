/* xHCI controller self-tests. Confirms the controller is up, the root hub was
 * scanned and the command/event ring round-trip works (a second No-Op). */
#include "xhci_tests.h"
#include "xhci.h"
#include "xhci_command.h"
#include "log.h"

void xhci_tests_run(void) {
    struct xhci *xhc = xhci_controller();
    if (!xhc || !xhc->initialized) {
        kernel_log_error("xhci tests: no controller");
        return;
    }
    if (xhc->max_ports == 0) {
        kernel_log_error("xhci tests: no root ports");
        return;
    }
    if (xhci_cmd_noop(xhc) != XHCI_CC_SUCCESS) {
        kernel_log_error("xhci tests: command ring round-trip failed");
        return;
    }
    kernel_log_line("[PASS] xhci controller test");
}
