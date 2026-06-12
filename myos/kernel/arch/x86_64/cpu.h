/* CPU type/register helpers (x86-64). */
#ifndef MYOS_X86_CPU_H
#define MYOS_X86_CPU_H

#include "types.h"

/* ---- Control / flag registers (cr.S) ------------------------------------- */
uint64_t x86_read_cr0(void);
uint64_t x86_read_cr2(void);
uint64_t x86_read_cr3(void);
uint64_t x86_read_cr4(void);

void x86_write_cr0(uint64_t value);
void x86_write_cr3(uint64_t value);
void x86_write_cr4(uint64_t value);

uint64_t x86_read_rflags(void);

/* ---- Descriptor-table / task-register loads (gdt_load.S) ----------------- */
void x86_lgdt(void *gdtr);
void x86_lidt(void *idtr);
void x86_ltr(uint16_t selector);
void gdt_reload_segments(void);    /* reload CS/DS/SS after lgdt */

/* ---- Halt (halt.S) ------------------------------------------------------- */
void x86_halt(void);               /* single hlt */
void x86_halt_forever(void) __attribute__((noreturn));

/* ---- Port I/O (port_io.S) ------------------------------------------------ */
uint8_t x86_inb(uint16_t port);
void    x86_outb(uint16_t port, uint8_t value);

/* ---- Misc inline helpers ------------------------------------------------- */
static inline void x86_cli(void) { __asm__ volatile("cli"); }
static inline void x86_sti(void) { __asm__ volatile("sti"); }
static inline void x86_invlpg(void *addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

/* RFLAGS bits. */
#define RFLAGS_IF (1ULL << 9)

/* ---- CPU state inspection (cpu.c) ---------------------------------------- */
struct x86_descriptor_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void cpu_read_gdtr(struct x86_descriptor_ptr *out);
void cpu_read_idtr(struct x86_descriptor_ptr *out);
int  cpu_state_readable(void);     /* reads cr0/cr4, returns 1 */
void cpu_dump_state(void);         /* logs cr0/cr2/cr3/cr4/rflags */

#endif /* MYOS_X86_CPU_H */
