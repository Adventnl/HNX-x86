/* Minimal ELF64 definitions required to load the kernel. */
#ifndef MYOS_ELF64_H
#define MYOS_ELF64_H

typedef unsigned short     Elf64_Half;
typedef unsigned int       Elf64_Word;
typedef int                Elf64_Sword;
typedef unsigned long long Elf64_Xword;
typedef long long          Elf64_Sxword;
typedef unsigned long long Elf64_Addr;
typedef unsigned long long Elf64_Off;

/* e_ident indices */
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6
#define EI_NIDENT  16

#define ELFMAG0    0x7F
#define ELFMAG1    'E'
#define ELFMAG2    'L'
#define ELFMAG3    'F'

#define ELFCLASS64  2
#define ELFDATA2LSB 1

#define ET_EXEC   2
#define EM_X86_64 62
#define PT_LOAD   1

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word  p_type;
    Elf64_Word  p_flags;
    Elf64_Off   p_offset;
    Elf64_Addr  p_vaddr;
    Elf64_Addr  p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

#endif /* MYOS_ELF64_H */
