/* Lock-light ring buffer of input events (producer: IRQ/injection). */
#ifndef MYOS_INPUT_QUEUE_H
#define MYOS_INPUT_QUEUE_H

#include "input_event.h"

void input_queue_init(void);
int  input_queue_push(const struct input_event *ev);   /* 0 ok, -1 full */
int  input_queue_pop(struct input_event *out);          /* 0 ok, -1 empty */
int  input_queue_count(void);

#endif /* MYOS_INPUT_QUEUE_H */
