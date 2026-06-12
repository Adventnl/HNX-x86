/* Stage 10: validate and load an ELF64 x86-64 executable kernel. */
#include "bootloader.h"

EFI_STATUS bl_elf_validate(void *file, UINTN size) {
    if (size < sizeof(Elf64_Ehdr)) {
        return EFI_LOAD_ERROR;
    }
    Elf64_Ehdr *e = (Elf64_Ehdr *)file;

    if (e->e_ident[EI_MAG0] != ELFMAG0 || e->e_ident[EI_MAG1] != ELFMAG1 ||
        e->e_ident[EI_MAG2] != ELFMAG2 || e->e_ident[EI_MAG3] != ELFMAG3) {
        return EFI_LOAD_ERROR;          /* not ELF */
    }
    if (e->e_ident[EI_CLASS] != ELFCLASS64) {
        return EFI_LOAD_ERROR;          /* not 64-bit */
    }
    if (e->e_ident[EI_DATA] != ELFDATA2LSB) {
        return EFI_LOAD_ERROR;          /* not little-endian */
    }
    if (e->e_type != ET_EXEC) {
        return EFI_LOAD_ERROR;          /* not executable */
    }
    if (e->e_machine != EM_X86_64) {
        return EFI_LOAD_ERROR;          /* not x86-64 */
    }
    if (e->e_phoff == 0 || e->e_phnum == 0) {
        return EFI_LOAD_ERROR;
    }
    return EFI_SUCCESS;
}

EFI_STATUS bl_elf_load(void *file, UINTN size, struct boot_kernel_info *out) {
    (void)size;
    Elf64_Ehdr *e = (Elf64_Ehdr *)file;
    unsigned char *base = (unsigned char *)file;

    /* First pass: compute the spanned physical range of all PT_LOAD segments. */
    UINT64 lo = ~0ULL;
    UINT64 hi = 0;
    for (Elf64_Half i = 0; i < e->e_phnum; i++) {
        Elf64_Phdr *p = (Elf64_Phdr *)(base + e->e_phoff + (UINT64)i * e->e_phentsize);
        if (p->p_type != PT_LOAD) {
            continue;
        }
        if (p->p_vaddr < lo) {
            lo = p->p_vaddr;
        }
        if (p->p_vaddr + p->p_memsz > hi) {
            hi = p->p_vaddr + p->p_memsz;
        }
    }
    if (lo == ~0ULL || hi <= lo) {
        return EFI_LOAD_ERROR;          /* no loadable segments */
    }

    UINT64 region_base = lo & ~EFI_PAGE_MASK;
    UINT64 region_end  = (hi + EFI_PAGE_MASK) & ~EFI_PAGE_MASK;
    UINTN  pages = (UINTN)((region_end - region_base) / EFI_PAGE_SIZE);

    /* Place the kernel at its link address (identity mapped under UEFI). */
    EFI_STATUS s = bs_alloc_pages_at((EFI_PHYSICAL_ADDRESS)region_base, pages);
    if (EFI_ERROR(s)) {
        return s;
    }

    /* Zero the whole region first so .bss (p_memsz > p_filesz) is cleared. */
    memset((void *)(UINTN)region_base, 0, (__SIZE_TYPE__)(region_end - region_base));

    /* Second pass: copy each PT_LOAD segment's file bytes to its vaddr. */
    for (Elf64_Half i = 0; i < e->e_phnum; i++) {
        Elf64_Phdr *p = (Elf64_Phdr *)(base + e->e_phoff + (UINT64)i * e->e_phentsize);
        if (p->p_type != PT_LOAD) {
            continue;
        }
        void *dst = (void *)(UINTN)p->p_vaddr;
        void *src = (void *)(base + p->p_offset);
        memcpy(dst, src, (__SIZE_TYPE__)p->p_filesz);
        /* Remaining (p_memsz - p_filesz) bytes are already zero. */
    }

    out->kernel_base  = region_base;
    out->kernel_size  = region_end - region_base;
    out->kernel_entry = e->e_entry;
    return EFI_SUCCESS;
}
