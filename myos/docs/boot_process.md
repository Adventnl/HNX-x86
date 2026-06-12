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
              ├─ load \boot\initramfs.hxf (HXF1)       [stage 55]
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
[OK] Initramfs file loaded
[OK] Memory map captured
[OK] ExitBootServices complete
```

The initramfs is allocated as `EfiLoaderData` (which the kernel's PMM never
frees and the VMM identity-maps), and its physical base/size are written to
`boot_info.initramfs_base` / `initramfs_size`. A missing initramfs is fatal:
the bootloader prints `[ERROR] Initramfs file load` and halts.

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
the kernel runs in its own address space (`[OK] Kernel CR3 loaded`). After the
switch, `vmm_validate_required_mappings()` reads CR3 back, re-translates the
kernel / framebuffer / boot_info / UEFI map / PMM bitmap, and round-trips a
scratch page (`[OK] VMM required mappings validated`).

## Verifying exceptions (Prompt 2.5)

Exception handling is not exercised by a normal boot (that would halt the
kernel). It is verified by dedicated destructive builds:

- `make verify-exception` builds with `-DMYOS_TEST_INVALID_OPCODE=1`, executes
  `ud2`, and asserts the serial log shows the `CPU EXCEPTION` dump with
  `vector : 0x6` / `#UD Invalid Opcode`.
- `make verify-pagefault` builds with `-DMYOS_TEST_PAGE_FAULT=1`, reads an
  unmapped canonical address, and asserts the `#PF Page Fault` dump with the
  faulting CR2 and decoded error-code bits.

Both run headless via `tools/verify_qemu.py` and confirm the trap frame carries
a correct kernel-mode RIP/CS/RFLAGS and an interrupted RSP inside the kernel
stack (SS = 0x10). Neither test is present in a normal `make all` / `make run`.

## Interrupts + scheduler (Prompt 3)

After the Prompt 2.5 baseline, `kernel_main` (now `MyOS Kernel 0.0.3`) brings
up the interrupt and scheduling foundation:

```
pic_disable()                  remap 8259 to 0x20/0x28 + mask all lines
madt_init(rsdp)                RSDP -> XSDT/RSDT -> MADT (CPUs, IOAPIC, IRQ0 override)
lapic_discover()               MSR 0x1B + MADT; map LAPIC 2MiB page CD/WT
lapic_enable()                 spurious vector 0xF0 | enable, TPR=0
irq_init()                     stubs for 0x20-0x2F, 0x30, 0xF0 -> IDT gates
pit_init_periodic(0)           free-running (calibration reference)
kernel_timer_init()            LAPIC timer @100Hz (PIT-calibrated) -> tick source
thread_system_init()           thread IDs + trampoline contract
scheduler_init()               boot placeholder + idle thread + ready queue
scheduler_tests_start()        threads A/B/C + checker created
scheduler_start()              abandon boot context; first context switch
```

From `scheduler_start()` on, all execution is in kernel threads. The timer IRQ
(vector 0x30) wakes sleepers and enforces the 50 ms quantum; the actual
preemption switch happens after EOI on the IRQ exit path. The scheduler
self-tests print `[TEST]`/`[PASS]` lines and `[OK] Scheduler tests passed`,
after which the system idles (checker sleeps, idle thread `sti; hlt`s).

Interrupts are first enabled not in `kernel_main` but inside the first
scheduled thread (`thread_trampoline` does `sti`), so no tick can arrive
before the scheduler is fully live.

## User/kernel boundary (Prompt 4)

After the scheduler is initialized (but before `scheduler_start`), `kernel_main`
(now `MyOS Kernel 0.0.4`) wires up the user/kernel boundary:

```
initramfs_init(base, size)     validate HXF1 header + entry table  -> [OK] Initramfs loaded/parsed
initramfs_dump()               list /bin/init.hxe, /bin/syscall_test.hxe, /etc/banner.txt
syscall_init()                 install int 0x80 (DPL 3) -> [OK] Syscall vector/dispatcher online
user_init()                    validate ring-3 selectors -> [OK] Ring 3 segments validated / entry online
scheduler_tests_start()        Prompt 3 self-tests still run
user_tests_start()             supervisor thread launches the user programs
scheduler_start()              hand off to the scheduler
```

### Ring-3 entry

Each user task gets a private address space (`user_address_space_create`): a
fresh PML4 that identity-maps the kernel's low footprint `[0, 4 MiB)`, the
framebuffer, and the LAPIC MMIO window as **supervisor** pages, plus the user
image (at `USER_IMAGE_BASE = 0x400000`) and stack as **user** pages. Because
the kernel runs from low identity memory, every user CR3 must mirror that
footprint so syscall/IRQ/fault handlers keep working while a user CR3 is active.
`USER_IMAGE_BASE` sits exactly at the 2 MiB boundary above the kernel image, so
user and kernel pages never overlap.

A user task is a kernel thread whose entry trampoline immediately calls
`user_enter_ring3(rip, rsp, cr3)` (in `user_entry.S`): it disables interrupts,
loads the user CR3, builds an `iretq` frame (SS=0x1B, RSP=user stack, RFLAGS
with IF set, CS=0x23, RIP=entry) and `iretq`s to CPL 3. On every context switch
the scheduler updates **TSS RSP0** to the incoming thread's kernel stack and
reloads **CR3** when the address space changes.

### Syscalls (`int 0x80`)

The user ABI is `rax`=number, args in `rdi, rsi, rdx, r10, r8, r9`, result in
`rax` (negative = error). `syscall_entry.S` saves the GPRs into a
`struct syscall_frame`, calls `syscall_dispatch`, writes the result back into
the saved `rax`, and `iretq`s. The Prompt 4 table:

| # | name | behavior |
|---|------|----------|
| 0 | exit   | mark task exited, store code, schedule away (never returns) |
| 1 | write  | fd 1/2 -> console; validates the user buffer; returns bytes written |
| 2 | read   | fd 0 returns 0 (EOF; no input device yet); len 0 returns 0 |
| 3 | sleep  | `thread_sleep_ms` |
| 4 | getpid | current user task id |
| 5 | yield  | `scheduler_yield` |

Invalid numbers return `-38` (`-ENOSYS`); bad user pointers return `-14`
(`-EFAULT`). The kernel never panics on bad user input — pointers are validated
in software (`user_range_is_valid`) before any access.

### Fault isolation

`x86_exception_dispatch` checks `CS.RPL`: a fault taken in ring 3 is routed to
`user_fault_handle`, which prints a diagnostic, logs `[OK] User fault isolated`,
marks the task `FAULTED`, and schedules away — the kernel keeps running. A
CPL-0 fault keeps the original fatal panic path. `make verify-user-fault`
launches a program that writes to an unmapped user address and asserts the
kernel survives.
