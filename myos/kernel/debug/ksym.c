/* Kernel symbol registry implementation (see kernel/debug/ksym.h). */
#include "ksym.h"

static struct ksym g_syms[KSYM_MAX];
static size_t      g_count;

void ksym_init(void) {
    g_count = 0;
}

int ksym_add(uint64_t addr, const char *name) {
    if (g_count >= KSYM_MAX) {
        return -1;
    }
    /* Insertion sort by address so resolve() can do a simple scan. */
    size_t i = g_count;
    while (i > 0 && g_syms[i - 1].addr > addr) {
        g_syms[i] = g_syms[i - 1];
        i--;
    }
    g_syms[i].addr = addr;
    g_syms[i].name = name;
    g_count++;
    return 0;
}

const char *ksym_resolve(uint64_t addr, uint64_t *offset_out) {
    const struct ksym *best = NULL;
    for (size_t i = 0; i < g_count; i++) {
        if (g_syms[i].addr <= addr) {
            best = &g_syms[i];
        } else {
            break;  /* sorted: no later symbol can be <= addr */
        }
    }
    if (!best) {
        if (offset_out) {
            *offset_out = 0;
        }
        return NULL;
    }
    if (offset_out) {
        *offset_out = addr - best->addr;
    }
    return best->name;
}

size_t ksym_count(void) {
    return g_count;
}
