/* Shared declarations for the MyOS UEFI bootloader. */
#ifndef MYOS_BOOTLOADER_H
#define MYOS_BOOTLOADER_H

#include "efi.h"
#include "efi_protocols.h"
#include "boot_services.h"
#include "elf64.h"
#include "boot_info.h"

/* ---- Globals (defined in efi_main.c) ------------------------------------- */
extern EFI_SYSTEM_TABLE  *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_HANDLE         gImageHandle;

/* ---- Logging (log.c) ----------------------------------------------------- */
void log_init(void);
void log_cstr(const char *s);          /* prints ASCII, '\n' -> CRLF        */
void log_ok(const char *msg);          /* prints "[OK] <msg>\n"             */
void log_err(const char *stage, EFI_STATUS status); /* "[ERROR] ..." block  */
void log_hex(myos_u64 value);          /* prints "0x" + 16 hex digits       */
void log_dec(myos_u64 value);

/* Print an error block for `stage` then halt forever. */
void bl_fail(const char *stage, EFI_STATUS status) __attribute__((noreturn));

/* ---- File loading (file.c) ----------------------------------------------- */
EFI_STATUS bl_load_file(CHAR16 *path, void **out_buffer, UINTN *out_size);

/* ---- ELF loader (elf_loader.c) ------------------------------------------- */
EFI_STATUS bl_elf_validate(void *file, UINTN size);
EFI_STATUS bl_elf_load(void *file, UINTN size, struct boot_kernel_info *out);

/* ---- Framebuffer / GOP (framebuffer.c) ----------------------------------- */
EFI_STATUS bl_get_framebuffer(struct boot_framebuffer *out);

/* ---- ACPI RSDP (acpi.c) -------------------------------------------------- */
EFI_STATUS bl_find_rsdp(myos_u64 *out_address);

/* ---- ExitBootServices (exit_boot.c) -------------------------------------- */
EFI_STATUS bl_capture_memory_map(struct boot_info *bi, UINTN *out_map_key);
EFI_STATUS bl_exit_boot_services(struct boot_info *bi, UINTN map_key);

/* ---- Kernel jump (jump.c) ------------------------------------------------ */
void bl_jump_to_kernel(myos_u64 entry, struct boot_info *bi) __attribute__((noreturn));

#endif /* MYOS_BOOTLOADER_H */
