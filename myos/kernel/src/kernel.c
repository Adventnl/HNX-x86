/* MyOS kernel entry: CPU + memory foundation bring-up (Prompt 2 / 2.5). */
#include "kernel.h"
#include "framebuffer_console.h"
#include "log.h"
#include "panic.h"
#include "serial.h"
#include "cpu.h"
#include "gdt.h"
#include "tss.h"
#include "idt.h"
#include "exceptions.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "early_tests.h"

static const struct boot_info *g_boot_info;

const struct boot_info *kernel_boot_info(void) {
    return g_boot_info;
}

/* Destructive verification hooks. These are compiled in only when the matching
 * MYOS_TEST_* macro is defined (via `make verify-exception` / `verify-pagefault`)
 * and never in a normal build. They run after the IDT/exceptions and the kernel
 * address space are live. */
static void run_destructive_tests(void) {
#if defined(MYOS_TEST_INVALID_OPCODE)
    kernel_log_line("[TEST-MODE] triggering invalid opcode (#UD)");
    __asm__ volatile("ud2");
    kernel_panic("ud2 did not fault");
#elif defined(MYOS_TEST_PAGE_FAULT)
    kernel_log_line("[TEST-MODE] triggering page fault (#PF)");
    volatile uint64_t *bad = (volatile uint64_t *)0xFFFFA00000000000ULL;
    uint64_t value = *bad;
    (void)value;
    kernel_panic("page fault did not fault");
#endif
}

void kernel_main(struct boot_info *bi) {
    /* Bring up COM1 first so the entire boot log is captured on serial. */
    serial_init();

    /* Validate boot_info enough to trust the framebuffer. */
    if (bi == NULL) {
        kernel_halt_forever();
    }
    if (bi->magic != MYOS_BOOT_INFO_MAGIC || bi->version != MYOS_BOOT_INFO_VERSION) {
        kernel_halt_forever();
    }
    g_boot_info = bi;

    /* --- Framebuffer console --- */
    fbcon_init(&bi->framebuffer);
    fbcon_set_color(0x00FFFFFF, 0x00000000);
    fbcon_clear(0x00000000);

    kernel_log_line("MyOS Kernel 0.0.2");
    kernel_log_ok("Kernel entered");
    kernel_log_ok("Boot info magic valid");
    kernel_log_ok("Framebuffer console online");
    if (fbcon_format_fallback()) {
        kernel_log_warn("framebuffer pixel format fallback");
    }

    /* --- Serial + logger (serial hardware already initialized above) --- */
    kernel_log_ok("Serial online");
    kernel_log_init();
    kernel_log_ok("Kernel logger online");

    /* --- CPU state --- */
    cpu_state_readable();
    cpu_dump_state();
    kernel_log_ok("CPU state readable");

    /* --- Descriptor tables + exceptions --- */
    gdt_init();
    kernel_log_ok("GDT loaded");
    tss_init();
    kernel_log_ok("TSS loaded");
    idt_init();
    kernel_log_ok("IDT loaded");
    exceptions_init();
    kernel_log_ok("Exceptions online");

    /* --- Physical memory --- */
    pmm_init(bi);
    kernel_log_ok("Physical memory map parsed");
    kernel_log_ok("Physical memory manager online");
    pmm_dump_stats();
    kernel_log_ok("Page allocator online");

    /* --- Virtual memory / kernel page tables --- */
    vmm_init(bi);
    kernel_log_ok("Kernel page tables built");
    vmm_load_kernel_address_space();
    if (vmm_cr3_loaded()) {
        kernel_log_ok("Kernel CR3 loaded");
    } else {
        kernel_log_warn("Custom CR3 built but not loaded");
    }
    kernel_log_hex64("    cr3 = ", x86_read_cr3());

    if (!vmm_validate_required_mappings(bi)) {
        kernel_panic("VMM required mappings invalid");
    }
    kernel_log_ok("VMM required mappings validated");
    kernel_log_ok("Page fault handler online");

    /* --- Heap --- */
    heap_init();
    kernel_log_ok("Kernel heap online");
    heap_dump_stats();

    /* --- Destructive verification hooks (test builds only) --- */
    run_destructive_tests();

    /* --- Early self-tests --- */
    early_tests_run();
    kernel_log_ok("Early kernel tests passed");

    kernel_log_line("");
    kernel_log_line("MyOS kernel foundation ready. Halting.");
    kernel_halt_forever();
}
