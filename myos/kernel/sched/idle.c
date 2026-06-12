/* Idle thread: runs only when the ready queue is empty. */
#include "idle.h"
#include "cpu.h"

void idle_thread_entry(void *arg) {
    (void)arg;
    for (;;) {
        /* sti;hlt — wake on the next interrupt (timer tick), which may
         * preempt to a newly woken thread. */
        x86_sti();
        x86_halt();
    }
}
