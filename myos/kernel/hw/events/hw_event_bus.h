/* Hardware event bus: a fixed-size ring of recent events plus a running total.
 * Lock-light (IRQ-save around the ring write) so input drivers can emit from
 * interrupt or poll context. */
#ifndef MYOS_HW_EVENT_BUS_H
#define MYOS_HW_EVENT_BUS_H

#include "types.h"
#include "hw_event.h"

void     hw_event_bus_init(void);
void     hw_event_emit(enum hw_event_type type, uint64_t a, uint64_t b, const char *message);
uint64_t hw_event_count(void);
void     hw_event_dump_recent(void);

/* Copy up to `max` most-recent events (newest last) into `out`. Returns count. */
int      hw_event_recent(struct hw_event *out, int max);

#endif /* MYOS_HW_EVENT_BUS_H */
