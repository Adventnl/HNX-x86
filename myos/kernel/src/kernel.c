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
#include "pic.h"
#include "madt.h"
#include "apic.h"
#include "irq.h"
#include "pit.h"
#include "timer.h"
#include "thread.h"
#include "scheduler.h"
#include "sleep.h"
#include "scheduler_tests.h"
#include "initramfs.h"
#include "user.h"
#include "syscall.h"
#include "user_tests.h"
#include "syscall_tests.h"
#include "vfs_tests.h"
#include "process_tests.h"
#include "vfs.h"
#include "ramfs.h"
#include "devfs.h"
#include "device.h"
#include "console.h"
#include "tty.h"
#include "process.h"
/* Prompt 5: storage + devices + input. */
#include "ioapic.h"
#include "driver.h"
#include "pci.h"
#include "pci_driver.h"
#include "block_registry.h"
#include "block_device.h"
#include "partition.h"
#include "ahci.h"
#include "nvme.h"
#include "hnxfs.h"
#include "ps2.h"
#include "keyboard.h"
#include "pci_tests.h"
#include "storage_tests.h"
#include "input_tests.h"

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

    kernel_log_line("MyOS Kernel 0.0.5");
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

    /* --- Prompt 2.5 early self-tests --- */
    early_tests_run();
    kernel_log_ok("Early kernel tests passed");
    kernel_log_ok("Prompt 2.5 baseline verification passed");

    /* ===== Prompt 3: interrupts + timers + scheduler ===== */

    /* --- Interrupt controllers --- */
    pic_disable();
    kernel_log_ok("Legacy PIC disabled");

    if (madt_init(bi->rsdp_address) != 0) {
        kernel_panic("ACPI MADT not found or invalid");
    }
    kernel_log_ok("ACPI MADT parsed");
    madt_dump_info();

    if (lapic_discover() != 0) {
        kernel_panic("Local APIC discovery failed");
    }
    kernel_log_ok("Local APIC discovered");
    kernel_log_hex64("    lapic mmio   : ", lapic_physical_base());

    lapic_enable();
    kernel_log_ok("Local APIC enabled");

    irq_init();
    kernel_log_ok("IRQ dispatcher online");

    /* --- Timers (PIT calibrates; LAPIC timer drives the tick) --- */
    pit_init_periodic(0);                 /* free-running for calibration */
    kernel_log_ok("PIT timer online");

    kernel_timer_init();                  /* logs LAPIC-timer / fallback line */
    kernel_log_ok("Kernel tick online");

    /* --- Threads + scheduler --- */
    thread_system_init();
    kernel_log_ok("Context switch online");

    scheduler_init();
    kernel_log_ok("Scheduler online");
    kernel_log_ok("Sleep/wakeup online");
    kernel_log_ok("Timer preemption online");
    kernel_log_ok("Prompt 3 baseline verification passed");

    /* ===== Prompt 4: userland foundation ===== */

    /* Initramfs (bootloader loaded it into RAM and filled boot_info). */
    initramfs_init(bi->initramfs_base, bi->initramfs_size);
    initramfs_dump();

    /* VFS + root ramfs (initramfs view) + devfs + console + TTY. */
    vfs_init();
    device_init();
    console_init();
    tty_init();

    struct filesystem *rootfs = ramfs_create_from_initramfs();
    if (!rootfs || vfs_mount("/", rootfs, NULL) != 0) {
        kernel_panic("vfs: cannot mount root ramfs");
    }
    struct filesystem *devfs = devfs_create();
    if (!devfs || vfs_mount("/dev", devfs, NULL) != 0) {
        kernel_panic("vfs: cannot mount devfs");
    }
    kernel_log_ok("File descriptor tables online");

    /* Process model + syscalls + ring-3 entry path + HXE1 loader. */
    process_system_init();
    syscall_init();
    user_init();
    kernel_log_ok("User executable loader online");

    /* Kernel-side unit checks (single-threaded here; kernel CR3 active). */
    syscall_tests_run();
    vfs_tests_run();
    process_tests_run();
    kernel_log_ok("Prompt 4 baseline verification passed");

    /* ===== Prompt 5: storage + device + input expansion ===== */

    /* Driver core + PCI enumeration. */
    driver_core_init();
    pci_init();
    pci_register_devices();

    /* Block layer + cache, then storage drivers via PCI matching. */
    block_init();
    ioapic_init();
    ahci_init();
    nvme_init();
    pci_driver_match_all();           /* probes AHCI (+ NVMe foundation) */

    /* Partitions over the discovered disks. */
    partition_init();
    partition_scan_all();

    /* Mount the persistent HNXFS from disk0p1 at /disk. */
    struct block_device *root_part = block_get_device("disk0p1");
    if (root_part) {
        struct filesystem *hfs = hnxfs_mount(root_part);
        if (hfs && vfs_mount("/disk", hfs, NULL) == 0) {
            kernel_log_ok("HNXFS mounted at /disk");
        } else {
            kernel_log_error("hnxfs: mount failed");
        }
    } else {
        kernel_log_error("hnxfs: disk0p1 not found");
    }

    /* PS/2 keyboard + interactive TTY input. */
    ps2_init();
    keyboard_init();
    tty_enable_canonical();

    /* Kernel-side Prompt 5 self-tests. */
    pci_tests_run();
    storage_tests_run();
    hnxfs_tests_run();
    input_tests_run();
    kernel_log_ok("Storage and device expansion tests passed");

    /* Pre-load the scripted shell session (raw lines), then the interactive
     * session (also pre-fed here; the shell -i prints prompts and reads it). */
    tty_push_line("echo hello from the MyOS shell");
    tty_push_line("pwd");
    tty_push_line("ls /");
    tty_push_line("ls /bin");
    tty_push_line("cat /etc/banner.txt");
    tty_push_line("lspci");
    tty_push_line("lsblk");
    tty_push_line("mounts");
    tty_push_line("ps");
    tty_push_line("exit");
    /* Interactive-mode script (consumed by /bin/shell.hxe -i). */
    tty_push_line("pwd");
    tty_push_line("ls /disk");
    tty_push_line("devices");
    tty_push_line("exit");

    /* Prompt 3 scheduler self-tests still run alongside the userland matrix. */
    scheduler_tests_start();

    /* Supervisor thread: spawns /bin/init.hxe (PID 1) and waits for it. */
    user_tests_start();

    /* Hand the CPU to the scheduler; the boot context is never resumed. */
    scheduler_start();
}
