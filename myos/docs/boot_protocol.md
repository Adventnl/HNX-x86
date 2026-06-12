# Boot Protocol (bootloader → kernel)

## Handoff ABI
- UEFI / bootloader code uses the **Microsoft x64 ABI**.
- The kernel uses the **System V AMD64 ABI**.
- The bootloader calls the entry through a `sysv_abi` function pointer:

```c
typedef void (__attribute__((sysv_abi)) *kernel_entry_t)(struct boot_info *);
```

This guarantees the single argument (`struct boot_info *`) is passed in **RDI**,
which is exactly what the kernel's `entry.S` expects. No assembly shim is needed
because clang accepts the `sysv_abi` attribute on the Windows target.

## Entry contract (`kernel_entry`)
On entry:
- `RDI` = `struct boot_info *`
- interrupts are the firmware's; the kernel immediately `cli`s
- paging is the firmware identity map; the kernel keeps using it

`entry.S` then:
1. `cli` / `cld`
2. loads `RSP` with the top of a 16 KiB static `.bss` stack
3. aligns `RSP` to 16 bytes, zeroes `RBP`
4. `call kernel_main`
5. halts forever if `kernel_main` returns

## `struct boot_info`
Defined in `shared/include/boot_info.h`, shared verbatim by both sides.

- `magic`   = `0x4D594F53424F4F` (`MYOS_BOOT_INFO_MAGIC`)
- `version` = `1`
- `size`    = `sizeof(struct boot_info)`
- `memory_map`  — base/size/descriptor_size/descriptor_version of the final
  UEFI memory map (the buffer lives in `EfiLoaderData`, valid after exit).
- `framebuffer` — base, size, width, height, pitch, bpp, pixel format + masks.
- `kernel`      — kernel base, size, entry (physical, identity mapped).
- `rsdp_address` — physical address of the ACPI RSDP.
- `initramfs_base/size` — reserved (0 in stages 1-16).

The kernel validates `bi != NULL`, `magic`, and `version` before trusting any
other field.

## Memory placement
- Kernel ELF is linked at physical `0x100000` and loaded there with
  `AllocatePages(AllocateAddress, EfiLoaderData)`.
- `boot_info`, the loaded kernel file, and the memory-map buffer are all
  `EfiLoaderData` allocations, so they persist past `ExitBootServices`.
