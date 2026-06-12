/* C exception dispatcher + page-fault diagnostics. */
#include "exceptions.h"
#include "cpu.h"
#include "log.h"
#include "panic.h"

static const char *names[32] = {
    "#DE Divide Error",
    "#DB Debug",
    "NMI",
    "#BP Breakpoint",
    "#OF Overflow",
    "#BR BOUND Range Exceeded",
    "#UD Invalid Opcode",
    "#NM Device Not Available",
    "#DF Double Fault",
    "Coprocessor Segment Overrun",
    "#TS Invalid TSS",
    "#NP Segment Not Present",
    "#SS Stack-Segment Fault",
    "#GP General Protection",
    "#PF Page Fault",
    "Reserved (15)",
    "#MF x87 Floating-Point",
    "#AC Alignment Check",
    "#MC Machine Check",
    "#XM SIMD Floating-Point",
    "#VE Virtualization",
    "#CP Control Protection",
    "Reserved (22)",
    "Reserved (23)",
    "Reserved (24)",
    "Reserved (25)",
    "Reserved (26)",
    "Reserved (27)",
    "#HV Hypervisor Injection",
    "#VC VMM Communication",
    "#SX Security Exception",
    "Reserved (31)",
};

const char *x86_exception_name(uint64_t vector) {
    if (vector < 32) {
        return names[vector];
    }
    return "Unknown";
}

void exceptions_init(void) {
    /* Vectors are installed by idt_init(); nothing further to wire up for
     * Prompt 2. Exposed as a distinct stage for clarity and future hooks. */
}

void page_fault_dump(struct x86_trap_frame *frame) {
    uint64_t cr2 = x86_read_cr2();
    uint64_t err = frame->error_code;

    kernel_log_line("--- page fault ---");
    kernel_log_hex64("  fault addr (cr2) : ", cr2);
    kernel_log_hex64("  error code       : ", err);
    kernel_log("  present : ");
    kernel_log_line((err & 0x1) ? "1 (protection)" : "0 (not-present)");
    kernel_log("  write   : ");
    kernel_log_line((err & 0x2) ? "1 (write)" : "0 (read)");
    kernel_log("  user    : ");
    kernel_log_line((err & 0x4) ? "1 (user)" : "0 (supervisor)");
    kernel_log("  reserved: ");
    kernel_log_line((err & 0x8) ? "1 (reserved bit set)" : "0");
    kernel_log("  insn fetch: ");
    kernel_log_line((err & 0x10) ? "1 (instruction fetch)" : "0");
    kernel_log_hex64("  rip              : ", frame->rip);
}

void x86_exception_dispatch(struct x86_trap_frame *frame) {
    x86_cli();

    kernel_log_line("");
    kernel_log_line("==================== CPU EXCEPTION ====================");
    kernel_log_hex64("  vector     : ", frame->vector);
    kernel_log("  name       : ");
    kernel_log_line(x86_exception_name(frame->vector));
    kernel_log_hex64("  error code : ", frame->error_code);
    kernel_log_hex64("  rip        : ", frame->rip);
    kernel_log_hex64("  rsp        : ", frame->rsp);
    kernel_log_hex64("  rflags     : ", frame->rflags);
    kernel_log_hex64("  cs         : ", frame->cs);
    /* Long mode always pushes SS:RSP; a NULL SS (allowed on same-CPL faults)
     * is reported as the kernel data selector for clarity. */
    kernel_log_hex64("  ss         : ", frame->ss ? frame->ss : 0x10);
    kernel_log("  mode       : ");
    kernel_log_line((frame->cs & 3) ? "user (CPL3)" : "kernel (CPL0)");

    if (frame->vector == VEC_PAGE_FAULT) {
        page_fault_dump(frame);
    }

    /* For Prompt 2, every exception is fatal. */
    kernel_panic("unhandled CPU exception");
}
