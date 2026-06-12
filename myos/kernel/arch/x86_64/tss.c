/* Task State Segment setup. */
#include "tss.h"
#include "gdt.h"
#include "cpu.h"

#define STACK_SIZE 16384

static struct tss g_tss __attribute__((aligned(16)));

/* Static kernel stacks: privilege-0 stack and IST stacks for fault handlers. */
static u8 rsp0_stack[STACK_SIZE] __attribute__((aligned(16)));
static u8 df_stack[STACK_SIZE]   __attribute__((aligned(16)));  /* double fault */
static u8 pf_stack[STACK_SIZE]   __attribute__((aligned(16)));  /* page fault   */

static uint64_t stack_top(u8 *base) {
    return (uint64_t)(uintptr_t)(base + STACK_SIZE);
}

void tss_init(void) {
    /* Zero the TSS. */
    u8 *p = (u8 *)&g_tss;
    for (unsigned i = 0; i < sizeof(g_tss); i++) {
        p[i] = 0;
    }

    g_tss.rsp0 = stack_top(rsp0_stack);
    g_tss.ist1 = stack_top(df_stack);   /* IST_DOUBLE_FAULT */
    g_tss.ist2 = stack_top(pf_stack);   /* IST_PAGE_FAULT   */
    g_tss.iomap_base = sizeof(struct tss);   /* no I/O bitmap */

    gdt_set_tss((uint64_t)(uintptr_t)&g_tss, sizeof(struct tss) - 1);
    x86_ltr(GDT_TSS);
}
