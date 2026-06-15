/* xHCI event ring consumer (polled). */
#ifndef MYOS_XHCI_EVENT_H
#define MYOS_XHCI_EVENT_H

#include "types.h"
#include "xhci.h"

/* Poll the event ring for a TRB of `want_type` (0 = any). Copies it to *out and
 * advances ERDP. Returns 1 if found within the spin budget, else 0. Events of
 * other types encountered along the way are consumed and dropped. */
int xhci_event_poll(struct xhci *xhc, uint8_t want_type, struct xhci_trb *out);

/* Drain and discard any currently-pending events (used to clear startup noise). */
void xhci_event_drain(struct xhci *xhc);

/* Non-blocking: consume one event if the controller has produced it. Returns 1
 * and copies it to *out, else 0. */
int xhci_event_try(struct xhci *xhc, struct xhci_trb *out);

#endif /* MYOS_XHCI_EVENT_H */
