/* Global process table: a fixed array of process slots keyed by monotonically
 * increasing PID. */
#ifndef MYOS_PROCESS_TABLE_H
#define MYOS_PROCESS_TABLE_H

#include "types.h"

struct process;
struct sys_ps_entry;

#define PROCESS_MAX 64

void process_table_init(void);

/* Allocate a zeroed process with a fresh PID, or NULL if the table is full. */
struct process *process_table_alloc(void);

/* Free a process slot (the caller has already released its resources). */
void process_table_free(struct process *p);

struct process *process_table_get(uint64_t pid);

/* Snapshot live processes into `out` (up to `max`); returns the count. */
int process_table_snapshot(struct sys_ps_entry *out, int max);

#endif /* MYOS_PROCESS_TABLE_H */
