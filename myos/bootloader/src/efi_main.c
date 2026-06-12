/* Stage 7 + orchestration: UEFI bootloader entry point (EfiMain).
 *
 * Performs the exact boot sequence:
 *   EfiMain -> init logging -> banner -> load kernel.elf -> validate ELF64 ->
 *   load PT_LOAD segments -> fill boot_info -> GOP framebuffer -> ACPI RSDP ->
 *   final memory map -> ExitBootServices -> jump to kernel(boot_info).
 */
#include "bootloader.h"

EFI_SYSTEM_TABLE  *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;
EFI_HANDLE         gImageHandle = NULL;

void bl_fail(const char *stage, EFI_STATUS status) {
    log_err(stage, status);
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    gImageHandle = image_handle;
    gST = system_table;
    gBS = system_table->BootServices;

    /* --- Stage 8: logging + banner --- */
    log_init();
    log_cstr("MyOS Bootloader\n");
    log_ok("UEFI entry");
    log_ok("Console initialized");

    /* --- Stage 11: allocate boot_info (before the final memory map) --- */
    struct boot_info *bi = (struct boot_info *)bs_alloc_pages(
        (UINTN)EFI_SIZE_TO_PAGES(sizeof(struct boot_info)));
    if (!bi) {
        bl_fail("boot_info allocation", EFI_OUT_OF_RESOURCES);
    }
    memset(bi, 0, sizeof(*bi));
    bi->magic   = MYOS_BOOT_INFO_MAGIC;
    bi->version = MYOS_BOOT_INFO_VERSION;
    bi->size    = (myos_u32)sizeof(struct boot_info);

    /* --- Stage 9: load \boot\kernel.elf --- */
    void *kfile = NULL;
    UINTN ksize = 0;
    EFI_STATUS s = bl_load_file((CHAR16 *)L"\\boot\\kernel.elf", &kfile, &ksize);
    if (EFI_ERROR(s)) {
        bl_fail("Kernel file load", s);
    }
    log_ok("Kernel file loaded");

    /* --- Stage 10: validate ELF64 header --- */
    s = bl_elf_validate(kfile, ksize);
    if (EFI_ERROR(s)) {
        bl_fail("ELF64 header validation", s);
    }
    log_ok("ELF64 header valid");

    /* --- Stage 10: load PT_LOAD segments --- */
    s = bl_elf_load(kfile, ksize, &bi->kernel);
    if (EFI_ERROR(s)) {
        bl_fail("Kernel LOAD segments", s);
    }
    log_ok("Kernel LOAD segments loaded");

    /* --- Stage 13: framebuffer from GOP --- */
    s = bl_get_framebuffer(&bi->framebuffer);
    if (EFI_ERROR(s)) {
        bl_fail("Framebuffer query", s);
    }
    log_ok("Framebuffer found");

    /* --- Stage 14: ACPI RSDP --- */
    s = bl_find_rsdp(&bi->rsdp_address);
    if (EFI_ERROR(s)) {
        bl_fail("ACPI RSDP search", s);
    }
    log_ok("ACPI RSDP found");

    /* --- Stage 12: final memory map (immediately before ExitBootServices) --- */
    UINTN map_key = 0;
    s = bl_capture_memory_map(bi, &map_key);
    if (EFI_ERROR(s)) {
        bl_fail("Memory map capture", s);
    }
    log_ok("Memory map captured");

    /* Last console line before we leave UEFI; next output is the kernel. */
    log_ok("ExitBootServices complete");

    /* --- Stage 15: ExitBootServices (with one retry on stale map key) --- */
    s = bl_exit_boot_services(bi, map_key);
    if (EFI_ERROR(s)) {
        /* If it returned an error, boot services are still active: log + halt. */
        bl_fail("ExitBootServices", s);
    }

    /* --- Stage 16: jump to kernel with boot_info in RDI (SysV ABI) --- */
    bl_jump_to_kernel(bi->kernel.kernel_entry, bi);
}
