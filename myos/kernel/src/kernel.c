/* MyOS kernel entry: CPU + memory foundation bring-up (Prompt 2). */
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

void kernel_main(struct boot_info *bi) {
    /* --- Validate boot_info enough to trust the framebuffer --- */
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

    /* --- Serial + logger --- */
    serial_init();
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
    kernel_log_ok("Page fault handler online");

    /* --- Heap --- */
    heap_init();
    kernel_log_ok("Kernel heap online");
    heap_dump_stats();

    /* --- Early self-tests --- */
    early_tests_run();
    kernel_log_ok("Early kernel tests passed");

    kernel_log_line("");
    kernel_log_line("MyOS kernel foundation ready. Halting.");
    kernel_halt_forever();
}
