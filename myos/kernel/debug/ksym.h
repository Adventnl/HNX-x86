/* Kernel symbol registry.
 *
 * Maps code addresses back to the nearest preceding symbol name + offset for
 * readable backtraces and panic dumps. The kernel is built at -O0 with frame
 * pointers, so a backtrace walks rbp; this table turns the raw return
 * addresses into "function+0xNN". Symbols are registered explicitly (a future
 * build step can emit a full table from `llvm-nm`); until then a curated set of
 * boot/entry symbols is registered so backtraces are not just hex.
 */
#ifndef MYOS_DEBUG_KSYM_H
#define MYOS_DEBUG_KSYM_H

#include "types.h"

#define KSYM_MAX 256

struct ksym {
    uint64_t    addr;
    const char *name;
};

void ksym_init(void);
/* Register a symbol; addresses may be added in any order. */
int  ksym_add(uint64_t addr, const char *name);
/* Resolve addr to the nearest symbol at or below it. Returns name (or NULL)
 * and stores the offset. */
const char *ksym_resolve(uint64_t addr, uint64_t *offset_out);
size_t ksym_count(void);

#endif /* MYOS_DEBUG_KSYM_H */
