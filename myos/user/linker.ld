/* User program link script. Linked at USER_IMAGE_BASE (4 MiB) with 4 KiB page
 * granularity so each PT_LOAD lands on a page boundary (mkhxe.py and the kernel
 * loader both map page-aligned). ld.lld is invoked with -z max-page-size=0x1000
 * to keep segments 4 KiB aligned (its x86-64 default is 2 MiB). */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;                 /* USER_IMAGE_BASE */

    .text ALIGN(0x1000) : {
        *(.text._start)
        *(.text .text.*)
    }

    .rodata ALIGN(0x1000) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(0x1000) : {
        *(.data .data.*)
    }

    .bss ALIGN(0x1000) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
    }
}
