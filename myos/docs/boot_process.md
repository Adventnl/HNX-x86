# Boot Process

End-to-end flow from firmware power-on to kernel text on screen.

```
UEFI firmware (OVMF)
  └─ loads  EFI/BOOT/BOOTX64.EFI   (our PE/COFF EFI application)
        └─ EfiMain(ImageHandle, SystemTable)        [stage 7]
              ├─ init logging + banner               [stage 8]
              ├─ allocate boot_info                   [stage 11]
              ├─ load \boot\kernel.elf into memory    [stage 9]
              ├─ validate ELF64 header                [stage 10]
              ├─ load PT_LOAD segments @ 0x100000     [stage 10]
              ├─ read GOP framebuffer                 [stage 13]
              ├─ find ACPI RSDP (config table)        [stage 14]
              ├─ capture final UEFI memory map        [stage 12]
              ├─ ExitBootServices (retry once)        [stage 15]
              └─ jump to kernel_entry(boot_info)      [stage 16]
                    └─ entry.S sets stack, calls kernel_main
                          └─ validates boot_info
                          └─ framebuffer console prints kernel banner
```

## UEFI rules honored
- UEFI boot services are only used **before** `ExitBootServices`.
- The console (`ConOut`) is never used after `ExitBootServices`.
- The final memory map is taken **immediately before** `ExitBootServices`.
- No allocation happens after the final memory-map call; the map buffer is
  pre-allocated and reused on retry.
- If `ExitBootServices` fails (stale map key) the map is re-fetched once and the
  call retried.
- The first output after `ExitBootServices` is the kernel framebuffer console.

## Progress output (UEFI console, pre-exit)
```
MyOS Bootloader
[OK] UEFI entry
[OK] Console initialized
[OK] Kernel file loaded
[OK] ELF64 header valid
[OK] Kernel LOAD segments loaded
[OK] Framebuffer found
[OK] ACPI RSDP found
[OK] Memory map captured
[OK] ExitBootServices complete
```

On failure the bootloader prints and halts forever:
```
[ERROR] <stage name>
status: 0x........
```

## Kernel initialization (Prompt 2)

After the jump, the kernel brings up the CPU and memory foundation. Output
goes to both the framebuffer console and COM1 serial.

```
kernel_entry (entry.S: stack + 16-byte align)
  └─ kernel_main(boot_info)
        ├─ validate boot_info (magic/version)        [enough to trust the FB]
        ├─ framebuffer console init
        ├─ serial_init (COM1 0x3F8, 38400 8N1)        [stage 19]
        ├─ kernel_log_init (FB + serial router)       [stage 20]
        ├─ cpu_dump_state (cr0/cr3/cr4/rflags)        [stage 18]
        ├─ gdt_init  (+ reload CS/DS/SS)              [stage 21]
        ├─ tss_init  (RSP0 + IST stacks, ltr)         [stage 22]
        ├─ idt_init  (vectors 0-31, interrupt gates)  [stage 23/24]
        ├─ exceptions_init (dispatcher + #PF dump)    [stage 25/26]
        ├─ pmm_init  (parse UEFI map, build bitmap)    [stage 28/29]
        ├─ vmm_init  (build kernel page tables)        [stage 30/31]
        ├─ vmm_load_kernel_address_space (load CR3)    [stage 31]
        ├─ heap_init (bump allocator)                  [stage 32]
        ├─ early_tests_run ([TEST]/[PASS])            [stage 33]
        └─ halt forever
```

CPU exceptions (vectors 0-31) are routed through assembly stubs in `isr.S`
into `x86_exception_dispatch`, which prints the vector, name, error code, RIP,
RSP, RFLAGS (and CR2 + decoded bits for page faults) and then panics. Double
faults and page faults run on dedicated IST stacks.

The custom kernel page tables (identity map of RAM + framebuffer, plus a
higher-half kernel mapping) are built from PMM pages and **CR3 is loaded**, so
the kernel runs in its own address space (`[OK] Kernel CR3 loaded`).
