/* Diagnostic dump framework.
 *
 * Subsystems register named dumpers; `dump_all()` runs them in registration
 * order. This gives a single entry point ("dump everything") for post-mortem
 * after a panic and a uniform way to expose state. Core dumpers (memory, slab,
 * objects, trace, log) live here; process/device/irq/mount/etc. dumpers are
 * registered by their owning subsystems.
 */
#ifndef MYOS_DEBUG_DUMP_H
#define MYOS_DEBUG_DUMP_H

#include "types.h"

#define DUMP_MAX_DUMPERS 32

typedef void (*dumper_fn)(void *ctx);

void debug_dump_init(void);
/* Register a dumper. Returns 0 on success, -1 if the table is full. */
int  debug_register_dumper(const char *name, dumper_fn fn, void *ctx);
size_t debug_dumper_count(void);

/* Run one named dumper (returns 0 if found) or all of them. */
int  debug_dump_one(const char *name);
void debug_dump_all(void);

/* Generic helpers usable by any dumper. */
void dump_hex(const void *data, size_t len, uint64_t base_addr);
void dump_memory(uint64_t addr, size_t len);

/* Core dumpers (also registered by debug_dump_init). */
void dump_slab(void *ctx);
void dump_objects(void *ctx);
void dump_trace(void *ctx);
void dump_log(void *ctx);
void dump_heap(void *ctx);

#endif /* MYOS_DEBUG_DUMP_H */
