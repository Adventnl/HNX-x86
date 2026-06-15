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
make user        # build all ring-3 programs (init, shell, coreutils, tests)
make initramfs   # pack build/image/initramfs.hxf (HXF1: /bin /tests /etc)
make storage-image # build storage.img (MBR: HNXFS p1 + scratch p2) + nvme.img
make image       # build build/image/myos.img (FAT: BOOTX64.EFI + kernel.elf + initramfs.hxf)
make run         # boot the image in QEMU + OVMF
make debug       # same as run but halted with a gdb stub on :1234
make clean       # remove build artifacts
```

### mtools is optional
`tools/build_image.py` builds the FAT image with `mtools` when present, and
otherwise falls back to a **built-in pure-Python FAT16 writer** (with VFAT long
names, so `boot/initramfs.hxf` resolves). No external dependency is required to
produce a bootable image on any host.

## Verification & inspection (Prompt 2.5 / Prompt 3)
```bash
make inspect            # file/ELF header/program headers/symbols of the binaries
make loc                # source line counts by category (excludes build/)
make run-headless       # run with no window; serial still on stdio
make verify-boot        # headless boot; assert the full boot log on serial
make verify-exception   # build with -DMYOS_TEST_INVALID_OPCODE; assert #UD dump
make verify-pagefault   # build with -DMYOS_TEST_PAGE_FAULT; assert #PF dump
make verify-interrupts  # PIC disabled, MADT parsed, LAPIC up, IRQ dispatcher up
make verify-timer       # PIT + LAPIC timer online, kernel ticks increasing
make verify-scheduler   # context switch + round-robin + sleep/wakeup tests pass
make verify-preemption  # quantum-expiry preemption observed
make verify-prompt3     # boot + #UD + #PF + interrupts + timer + sched + preempt
make verify-qemu-matrix # boot-verify across 128M/256M/512M/1024M/2048M
```

## Userland foundation build + verification (Prompt 4)
```bash
make verify-user-build  # all /bin + /tests HXE programs + initramfs.hxf exist
make verify-initramfs   # the archive contains every required /bin /tests /etc file
make verify-user-mode   # [OK] Ring 3 entry online + [USER] hello from ring 3
make verify-syscalls    # [PASS] syscall_test
make verify-vfs         # [PASS] vfs_test + [PASS] fd_test
make verify-process     # [PASS] spawn_test
make verify-shell       # [PASS] shell scripted session
make verify-user-fault  # [OK] User fault isolated + Userland foundation tests passed
make verify-prompt4     # all Prompt 3 targets + all of the above
```

The HXE inspector `tools/inspect_hxe.py <file>` and the initramfs inspector
`tools/inspect_initramfs.py <archive> [--require <path>]` dump/validate the
custom formats without booting.

## Storage + device + input verification (Prompt 5)
```bash
make storage-image          # build storage.img (HNXFS) + nvme.img
make verify-pci             # PCI bus scanned + pci enumeration
make verify-block           # block layer online + block cache + partition parser
make verify-storage         # AHCI block device online + disk read + disk write
make verify-hnxfs           # HNXFS mounted + create/write/read/mkdir/unlink
make verify-keyboard        # PS/2 controller + keyboard input + scripted injection
make verify-tty             # TTY interactive input + canonical input + shell smoke
make verify-expanded-userland  # expanded coreutils + storage user programs
make verify-prompt5         # verify-prompt4 + all of the above + qemu-matrix
```
QEMU automatically attaches `build/image/storage.img` (SATA via `ich9-ahci`) and
`build/image/nvme.img` (NVMe) when they sit next to `myos.img`; `make image`
builds them. Disk/FS inspectors: `tools/disk/inspect_disk.py`,
`tools/fs/inspect_hnxfs.py`, `tools/pci/pci_ids_min.py`.

### User-program compiler flags (ring 3, freestanding)
```
--target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector
-fno-builtin -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx -msoft-float
-nostdlib -Wall -Wextra   (-Iuser/include -Ikernel/user for the shared ABI)
```
Linked with `ld.lld -T user/linker.ld -z max-page-size=0x1000` (4 KiB page
granularity; ld.lld's x86-64 default is 2 MiB), then converted to HXE1 by
`tools/mkhxe.py`. The initramfs is packed by `tools/mkinitramfs.py` (HXF1).
Each program links `crt0.o` + its objects + the user runtime
(`syscall/unistd/stdio/string/stdlib/malloc`); the shell also links
`parser.o`/`builtins.o`.

### Kernel FP/SIMD policy (Prompt 3)
The kernel is compiled with `-mno-sse -mno-sse2 -mno-mmx -msoft-float`. The
IRQ stubs save only general-purpose registers, so the kernel must never touch
FP/SIMD state — an asynchronous timer interrupt would silently corrupt it
otherwise. This is a permanent kernel-mode rule (user-space FP state handling
arrives with the user/kernel boundary work).
The `verify-*` targets use `tools/verify_qemu.py`, which runs QEMU headless,
captures COM1 serial to `build/image/<name>.log`, and prints `[PASS]`/`[FAIL]`.
No GUI is required. `verify-exception`/`verify-pagefault` build a *separate*
destructive test kernel and restore the normal image afterwards — destructive
tests are never present in a normal `make all` / `make image`.

### Destructive test flags (compile-time, opt-in only)
```
MYOS_TEST_INVALID_OPCODE   ud2 after init   -> #UD
MYOS_TEST_PAGE_FAULT       read unmapped VA -> #PF
MYOS_TEST_PMM_STRESS       extended PMM stress in early tests
MYOS_TEST_VERBOSE          extra diagnostic logging
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

## Kernel source layout
The kernel is built from these source directories (object names are flattened
into `build/kernel/`; basenames are unique across dirs):
```
kernel/src/          kernel.c framebuffer_console.c string.c panic.c log.c assert.c
kernel/arch/x86_64/  entry.S cr.S halt.S port_io.S gdt_load.S isr.S irq_stubs.S
                     cpu.c serial.c gdt.c tss.c idt.c exceptions.c paging.c
                     pic.c madt.c apic.c irq.c pit.c lapic_timer.c timer.c
kernel/memory/       pmm.c vmm.c heap.c (+ memory_layout.h)
kernel/sched/        thread.c scheduler.c idle.c sleep.c context_switch.S
kernel/initramfs/    initramfs.c              (HXF1 parser)
kernel/user/         user_entry.S syscall_entry.S user.c user_address_space.c
                     user_loader.c user_copy.c user_fault.c syscall.c syscall_table.c
kernel/fs/           vfs.c inode.c file.c path.c + ramfs/ramfs.c + devfs/devfs.c
kernel/device/       device.c char_device.c
kernel/tty/          console.c tty.c
kernel/process/      process.c process_table.c fd_table.c exec.c wait.c
kernel/tests/        early_tests.c scheduler_tests.c user_tests.c
                     syscall_tests.c vfs_tests.c process_tests.c
```
Include paths add `-Ikernel/fs -Ikernel/fs/ramfs -Ikernel/fs/devfs -Ikernel/device
-Ikernel/tty -Ikernel/process` to the earlier set. Same freestanding flags,
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

## Prompt 6 verification targets

```bash
make verify-msi               # PCI caps + MSI/MSI-X foundation
make verify-driver-lifecycle  # lifecycle state machine + hardware event bus
make verify-xhci              # xHCI controller bring-up + root hub scan
make verify-usb               # USB core + descriptor parser + enumeration
make verify-hid               # HID keyboard/mouse online + report tests
make verify-input-unified     # PS/2 + USB keyboard + mouse + TTY unification
make verify-hw-userland       # hwinfo/drivers/devtree/lsusb/hidinfo/inputtest
make verify-prompt6           # all of the above + verify-prompt5 + memory matrix
```

Each boots the image headlessly in QEMU + OVMF and greps the COM1 serial log for required markers. QEMU now also attaches `-device qemu-xhci -device usb-kbd -device usb-mouse` (see tools/run_qemu.py / tools/verify_qemu.py). New offline tooling: `tools/usb/decode_usb_descriptor.py`, `tools/usb/decode_hid_report.py`, `tools/hw/inspect_devices.py`, `tools/hw/inspect_interrupts.py`.
