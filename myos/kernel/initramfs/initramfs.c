/* HXF1 initramfs parser. The archive lives in identity-mapped RAM, so entries
 * are referenced directly by (base + entry.offset) without any copy. */
#include "initramfs.h"
#include "kernel.h"
#include "log.h"

static uint64_t g_base;
static uint64_t g_size;
static const struct hxf_header *g_header;
static const struct hxf_entry  *g_entries;
static int g_available;

static int path_eq(const char *a, const char *b) {
    for (uint64_t i = 0; i < HXF_PATH_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            return 1;
        }
    }
    return 1;   /* both filled the whole field identically */
}

void initramfs_init(uint64_t base, uint64_t size) {
    g_available = 0;
    g_base = base;
    g_size = size;
    g_header = NULL;
    g_entries = NULL;

    if (base == 0 || size < sizeof(struct hxf_header)) {
        kernel_log_error("initramfs: missing or too small");
        return;
    }
    kernel_log_ok("Initramfs loaded");

    const struct hxf_header *h = (const struct hxf_header *)(uintptr_t)base;
    if (h->magic != HXF_MAGIC) {
        kernel_log_error("initramfs: bad magic");
        return;
    }
    if (h->version != HXF_VERSION) {
        kernel_log_error("initramfs: unsupported version");
        return;
    }
    if (h->header_size < sizeof(struct hxf_header)) {
        kernel_log_error("initramfs: bad header size");
        return;
    }

    uint64_t table_bytes = (uint64_t)h->entry_count * sizeof(struct hxf_entry);
    if (h->header_size + table_bytes > size) {
        kernel_log_error("initramfs: entry table out of bounds");
        return;
    }

    g_header = h;
    g_entries = (const struct hxf_entry *)(uintptr_t)(base + h->header_size);

    /* Bounds-check every blob so later lookups can trust the table. */
    for (uint32_t i = 0; i < h->entry_count; i++) {
        const struct hxf_entry *e = &g_entries[i];
        if (e->offset > size || e->size > size || e->offset + e->size > size) {
            kernel_log_error("initramfs: entry blob out of bounds");
            g_header = NULL;
            g_entries = NULL;
            return;
        }
    }

    g_available = 1;
    kernel_log_ok("Initramfs parsed");
    kernel_log_hex64("    entries     : ", g_header->entry_count);
    kernel_log_hex64("    archive size: ", g_size);
}

int initramfs_is_available(void) {
    return g_available;
}

const void *initramfs_find(const char *path, uint64_t *out_size) {
    if (!g_available || !path) {
        return NULL;
    }
    for (uint32_t i = 0; i < g_header->entry_count; i++) {
        const struct hxf_entry *e = &g_entries[i];
        if (path_eq(e->path, path)) {
            if (out_size) {
                *out_size = e->size;
            }
            return (const void *)(uintptr_t)(g_base + e->offset);
        }
    }
    return NULL;
}

void initramfs_dump(void) {
    if (!g_available) {
        kernel_log_line("    (initramfs unavailable)");
        return;
    }
    for (uint32_t i = 0; i < g_header->entry_count; i++) {
        const struct hxf_entry *e = &g_entries[i];
        kernel_log("    ");
        kernel_log(e->path);
        kernel_log_hex64("  size=", e->size);
    }
}
