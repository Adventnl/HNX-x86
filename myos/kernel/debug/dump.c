/* Diagnostic dump framework implementation (see kernel/debug/dump.h). */
#include "dump.h"
#include "slab.h"
#include "object.h"
#include "ktrace.h"
#include "klog_ring.h"
#include "heap.h"
#include "log.h"
#include "fmt.h"

struct dumper_entry {
    const char *name;
    dumper_fn   fn;
    void       *ctx;
};

static struct dumper_entry g_dumpers[DUMP_MAX_DUMPERS];
static size_t              g_count;

void dump_hex(const void *data, size_t len, uint64_t base_addr) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t off = 0; off < len; off += 16) {
        char line[80];
        int n = ksnprintf(line, sizeof(line), "%p: ", (void *)(base_addr + off));
        for (size_t i = 0; i < 16 && off + i < len; i++) {
            n += ksnprintf(line + n, sizeof(line) - n, "%02x ", p[off + i]);
        }
        /* ASCII column. */
        n += ksnprintf(line + n, sizeof(line) - n, " ");
        for (size_t i = 0; i < 16 && off + i < len; i++) {
            uint8_t c = p[off + i];
            char ch = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
            if ((size_t)n + 1 < sizeof(line)) {
                line[n++] = ch;
            }
        }
        line[n] = '\0';
        kernel_log_line(line);
    }
}

void dump_memory(uint64_t addr, size_t len) {
    dump_hex((const void *)(uintptr_t)addr, len, addr);
}

void dump_slab(void *ctx) {
    (void)ctx;
    struct kmem_stats st;
    kmem_get_stats(&st);
    kdprintf("slab: allocs=%u frees=%u live=%u bytes=%u large=%u caches=%u\n",
             (unsigned)st.total_allocs, (unsigned)st.total_frees,
             (unsigned)st.live_objects, (unsigned)st.bytes_live,
             (unsigned)st.large_allocs, (unsigned)st.cache_count);
    kmem_dump_caches();
}

void dump_objects(void *ctx) {
    (void)ctx;
    kdprintf("kobjects: total=%u\n", (unsigned)kobject_count());
    for (int t = KOBJ_NONE; t < KOBJ_MAX; t++) {
        size_t n = kobject_count_by_type((enum kobject_type)t);
        if (n) {
            kdprintf("  %s: %u\n", kobject_type_name((enum kobject_type)t),
                     (unsigned)n);
        }
    }
}

void dump_trace(void *ctx) {
    (void)ctx;
    ktrace_dump(16);
}

void dump_log(void *ctx) {
    (void)ctx;
    kdprintf("klog ring: used=%u total=%u dropped=%u\n",
             (unsigned)klog_ring_used(), (unsigned)klog_ring_total_bytes(),
             (unsigned)klog_ring_dropped());
}

void dump_heap(void *ctx) {
    (void)ctx;
    heap_dump_stats();
}

void debug_dump_init(void) {
    g_count = 0;
    debug_register_dumper("heap", dump_heap, NULL);
    debug_register_dumper("slab", dump_slab, NULL);
    debug_register_dumper("objects", dump_objects, NULL);
    debug_register_dumper("trace", dump_trace, NULL);
    debug_register_dumper("log", dump_log, NULL);
}

int debug_register_dumper(const char *name, dumper_fn fn, void *ctx) {
    if (g_count >= DUMP_MAX_DUMPERS) {
        return -1;
    }
    g_dumpers[g_count].name = name;
    g_dumpers[g_count].fn = fn;
    g_dumpers[g_count].ctx = ctx;
    g_count++;
    return 0;
}

size_t debug_dumper_count(void) {
    return g_count;
}

int debug_dump_one(const char *name) {
    for (size_t i = 0; i < g_count; i++) {
        const char *a = g_dumpers[i].name;
        const char *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) {  /* both at NUL => equal */
            kdprintf("==== dump: %s ====\n", g_dumpers[i].name);
            g_dumpers[i].fn(g_dumpers[i].ctx);
            return 0;
        }
    }
    return -1;
}

void debug_dump_all(void) {
    kernel_log_line("######## DEBUG DUMP (all) ########");
    for (size_t i = 0; i < g_count; i++) {
        kdprintf("==== dump: %s ====\n", g_dumpers[i].name);
        g_dumpers[i].fn(g_dumpers[i].ctx);
    }
    kernel_log_line("######## END DEBUG DUMP ########");
}
