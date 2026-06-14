/* Process table. */
#include "process_table.h"
#include "process.h"
#include "syscall_abi.h"
#include "heap.h"
#include "string.h"
#include "irq.h"
#include "log.h"

static struct process *g_table[PROCESS_MAX];
static uint64_t g_next_pid;

void process_table_init(void) {
    for (int i = 0; i < PROCESS_MAX; i++) {
        g_table[i] = NULL;
    }
    g_next_pid = 1;
    kernel_log_ok("Process table online");
}

struct process *process_table_alloc(void) {
    uint64_t flags = irq_save_flags_and_disable();
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (!g_table[i]) {
            struct process *p = (struct process *)kcalloc(1, sizeof(struct process));
            if (!p) {
                irq_restore_flags(flags);
                return NULL;
            }
            p->pid = g_next_pid++;
            g_table[i] = p;
            irq_restore_flags(flags);
            return p;
        }
    }
    irq_restore_flags(flags);
    return NULL;
}

void process_table_free(struct process *p) {
    if (!p) {
        return;
    }
    uint64_t flags = irq_save_flags_and_disable();
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (g_table[i] == p) {
            g_table[i] = NULL;
            irq_restore_flags(flags);
            kfree(p);
            return;
        }
    }
    irq_restore_flags(flags);
}

struct process *process_table_get(uint64_t pid) {
    for (int i = 0; i < PROCESS_MAX; i++) {
        if (g_table[i] && g_table[i]->pid == pid) {
            return g_table[i];
        }
    }
    return NULL;
}

int process_table_snapshot(struct sys_ps_entry *out, int max) {
    int n = 0;
    for (int i = 0; i < PROCESS_MAX && n < max; i++) {
        struct process *p = g_table[i];
        if (!p) {
            continue;
        }
        out[n].pid = p->pid;
        out[n].parent_pid = p->parent_pid;
        out[n].state = (unsigned int)p->state;
        out[n]._pad = 0;
        strlcpy(out[n].name, p->name, sizeof(out[n].name));
        n++;
    }
    return n;
}
