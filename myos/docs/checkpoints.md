# Checkpoints

## Prompt 1 — boot pipeline (stages 1–16)

| # | Stage | Where | Done |
|---|-------|-------|------|
| 1 | Project folder structure | whole tree | ✅ |
| 2 | Build system | `Makefile` | ✅ |
| 3 | Cross-compilation setup | clang targets in `Makefile` | ✅ |
| 4 | Linker script | `kernel/arch/x86_64/linker.ld` | ✅ |
| 5 | QEMU boot test setup | `tools/run_qemu.py` | ✅ |
| 6 | UEFI disk image builder | `tools/build_image.py` | ✅ |
| 7 | UEFI bootloader entry point | `bootloader/src/efi_main.c` | ✅ |
| 8 | UEFI screen/log output | `bootloader/src/log.c` | ✅ |
| 9 | UEFI file loading | `bootloader/src/file.c` | ✅ |
| 10 | Kernel ELF loader | `bootloader/src/elf_loader.c` | ✅ |
| 11 | Boot information structure | `shared/include/boot_info.h` | ✅ |
| 12 | UEFI memory map reader | `bootloader/src/exit_boot.c` | ✅ |
| 13 | UEFI framebuffer/GOP reader | `bootloader/src/framebuffer.c` | ✅ |
| 14 | ACPI RSDP finder | `bootloader/src/acpi.c` | ✅ |
| 15 | ExitBootServices logic | `bootloader/src/exit_boot.c` | ✅ |
| 16 | Kernel jump logic | `bootloader/src/jump.c` | ✅ |

## Prompt 2 — kernel CPU + memory foundation (stages 17–34)

| # | Stage | Where | Done |
|---|-------|-------|------|
| 17 | Kernel arch folder expansion | `kernel/arch/x86_64/*`, `kernel/memory/*`, `kernel/tests/*` | ✅ |
| 18 | CPU type/register definitions | `kernel/arch/x86_64/cpu.{c,h}`, `cr.S`, `port_io.S` | ✅ |
| 19 | Early serial COM1 logging | `kernel/arch/x86_64/serial.{c,h}` | ✅ |
| 20 | Kernel logging router | `kernel/src/log.c`, `kernel/include/log.h` | ✅ |
| 21 | GDT setup | `kernel/arch/x86_64/gdt.{c,h}`, `gdt_load.S` | ✅ |
| 22 | TSS setup | `kernel/arch/x86_64/tss.{c,h}` | ✅ |
| 23 | IDT setup | `kernel/arch/x86_64/idt.{c,h}` | ✅ |
| 24 | Exception assembly stubs | `kernel/arch/x86_64/isr.S` | ✅ |
| 25 | C exception dispatcher | `kernel/arch/x86_64/exceptions.{c,h}` | ✅ |
| 26 | Page fault handler | `kernel/arch/x86_64/exceptions.c` (`page_fault_dump`) | ✅ |
| 27 | CPU halt + panic hardening | `kernel/src/panic.c`, `panic.h`, `halt.S` | ✅ |
| 28 | Physical memory map parser | `kernel/memory/pmm.c` | ✅ |
| 29 | Physical page allocator | `kernel/memory/pmm.{c,h}` (bitmap) | ✅ |
| 30 | Kernel virtual memory constants | `kernel/memory/memory_layout.h` | ✅ |
| 31 | Kernel page table builder | `kernel/memory/vmm.c`, `kernel/arch/x86_64/paging.{c,h}` | ✅ |
| 32 | Kernel heap allocator | `kernel/memory/heap.{c,h}` (bump) | ✅ |
| 33 | Early kernel test framework | `kernel/tests/early_tests.{c,h}` | ✅ |
| 34 | Documentation + validation | `docs/*`, `README.md` | ✅ |

## PMM design
- Bitmap allocator, 1 bit per 4 KiB page (set = used/reserved).
- The bitmap is sized to the highest **RAM** address (a positive type whitelist
  excludes reserved/MMIO/unusable regions, e.g. the 64-bit PCI hole, so the
  bitmap stays small).
- Start fully used → free every `EfiConventionalMemory` region → re-reserve
  page 0, the kernel image, `boot_info`, the UEFI memory map, and the bitmap.
- Framebuffer / ACPI / UEFI-runtime ranges are non-conventional and are never
  freed, so they remain reserved automatically.
- `pmm_free_page` rejects misaligned, out-of-range, and double frees.

## VMM design
- Kernel-owned PML4 built from PMM pages.
- **Identity map** of all RAM (+ low 4 GiB floor) with 2 MiB pages; the
  framebuffer range is mapped cache-disabled/write-through. This single mapping
  satisfies every required mapping (kernel code/data/bss, stack, framebuffer,
  `boot_info`, UEFI map, PMM bitmap, low identity for the running code).
- Kernel image additionally mapped into the higher half
  (`KERNEL_HIGHER_HALF_BASE`) for future use.
- `vmm_map_page` / `vmm_unmap_page` / `vmm_get_physical` operate on the kernel
  PML4 with 4 KiB pages and `invlpg`.

## Custom CR3
**Loaded.** `vmm_load_kernel_address_space()` activates the kernel page tables
and the boot prints `[OK] Kernel CR3 loaded` (verified: `cr3 = 0x1000`). The
identity map guarantees the running code, stack, and all data structures remain
addressable across the switch.

## Heap
Early **bump allocator** over a 2 MiB static `.bss` arena (no VMM mapping
needed — it is part of the identity/higher-half-mapped kernel image). `kfree`
is a documented no-op for now.

## Acceptance
`make clean && make all && make image && make run` boots and prints, via both
the framebuffer console and COM1 serial:

```
MyOS Kernel 0.0.2
[OK] Kernel entered
[OK] Boot info magic valid
[OK] Framebuffer console online
[OK] Serial online
[OK] Kernel logger online
[OK] CPU state readable
[OK] GDT loaded
[OK] TSS loaded
[OK] IDT loaded
[OK] Exceptions online
[OK] Physical memory map parsed
[OK] Physical memory manager online
[OK] Page allocator online
[OK] Kernel page tables built
[OK] Kernel CR3 loaded
[OK] Page fault handler online
[OK] Kernel heap online
[OK] Early kernel tests passed
```

## Next milestone (after Prompt 2)
**Prompt 3 — interrupt controller and scheduler foundation:** PIC disable
hardening, Local APIC discovery/init, timer source, IRQ routing, kernel
threads, context switching, round-robin scheduler, sleep/wakeup, timer
preemption.
