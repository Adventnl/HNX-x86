# Build

## Host dependencies
| Tool | Purpose | Install (macOS) | Install (Debian/Ubuntu) |
|------|---------|-----------------|--------------------------|
| `clang` | compile bootloader (COFF) + kernel (ELF) | `brew install llvm` | `apt-get install clang` |
| `lld-link`, `ld.lld` | link EFI app + kernel | `brew install lld` | `apt-get install lld` |
| `mtools` | build FAT image (no root) | `brew install mtools` | `apt-get install mtools` |
| `qemu-system-x86_64` | run | `brew install qemu` | `apt-get install qemu-system-x86` |
| OVMF / edk2 firmware | UEFI for QEMU | bundled with `brew install qemu` | `apt-get install ovmf` |
| `python3` | image + run scripts | preinstalled | `apt-get install python3` |

Apple `clang` can target both `x86_64-unknown-windows` (COFF) and
`x86_64-unknown-none-elf` (ELF), so a separate cross-gcc is not required — only
the LLVM linkers (`lld`).

## Commands
```bash
make all         # build BOOTX64.EFI + kernel.elf
make bootloader  # build only the EFI application
make kernel      # build only the kernel ELF
make image       # build build/image/myos.img (FAT, contains both)
make run         # boot the image in QEMU + OVMF
make debug       # same as run but halted with a gdb stub on :1234
make clean       # remove build artifacts
```

## Compiler flags
**Bootloader** (`--target=x86_64-unknown-windows`):
`-ffreestanding -fshort-wchar -mno-red-zone -fno-stack-protector -fno-builtin
-Wall -Wextra`, linked with
`lld-link /subsystem:efi_application /entry:EfiMain /nodefaultlib`.

**Kernel** (`--target=x86_64-unknown-none-elf`):
`-ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie
-mno-red-zone -mcmodel=kernel -nostdlib -Wall -Wextra`, linked with
`ld.lld -T kernel/arch/x86_64/linker.ld`.

## Outputs
```
build/bootloader/BOOTX64.EFI
build/kernel/kernel.elf
build/image/myos.img
build/image/qemu.log         (guest-error log from `make run`)
```

## Kernel source layout (Prompt 2)
The kernel is built from four source directories (object names are flattened
into `build/kernel/`):
```
kernel/src/          kernel.c framebuffer_console.c string.c panic.c log.c assert.c
kernel/arch/x86_64/  entry.S cr.S halt.S port_io.S gdt_load.S isr.S
                     cpu.c serial.c gdt.c tss.c idt.c exceptions.c paging.c
kernel/memory/       pmm.c vmm.c heap.c (+ memory_layout.h)
kernel/tests/        early_tests.c
```
Include paths: `-Ikernel/include -Ikernel/arch/x86_64 -Ikernel/memory
-Ikernel/tests -Ishared/include`. Same freestanding flags as Prompt 1,
`-Wall -Wextra`, no warnings.

## Serial console
The kernel logs to both the framebuffer and COM1. `make run` passes
`-serial stdio`, so the kernel boot log appears in your terminal. Extra
`tools/run_qemu.py` flags:
```
--debug     # -s -S : start halted, gdb stub on :1234 (same as `make debug`)
--headless  # -display none : no window; serial still on stdio (CI / no desktop)
```

## Debugging with gdb
```bash
make debug                 # terminal 1
gdb build/kernel/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```
