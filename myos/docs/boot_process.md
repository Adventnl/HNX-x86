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

## Userland foundation (Prompt 4)

After the scheduler is initialized (but before `scheduler_start`), `kernel_main`
(`MyOS Kernel 0.0.4`) wires up the entire userland foundation:

```
[OK] Prompt 3 baseline verification passed
initramfs_init(base, size)     validate HXF1 -> [OK] Initramfs loaded/parsed
vfs_init()                     mount table -> [OK] VFS online
device_init()                  register null/zero
console_init()                 register /dev/console -> [OK] /dev/console online
tty_init()                     scripted input buffer -> [OK] TTY layer online
ramfs_create_from_initramfs()  build tree from HXF1, mount at "/"
devfs_create()                 mount at "/dev" -> [OK] devfs online
                               -> [OK] File descriptor tables online
process_system_init()          process table -> [OK] Process table online
syscall_init()                 int 0x80 (DPL 3) -> [OK] Syscall vector/dispatcher online
user_init()                    ring-3 selectors -> [OK] Ring 3 entry online
                               -> [OK] User executable loader online
syscall/vfs/process_tests_run  kernel-side unit checks ([PASS] kernel …)
tty_push_line(...)             pre-load the scripted shell session
scheduler_tests_start()        Prompt 3 self-tests still run alongside
user_tests_start()             supervisor spawns /bin/init.hxe (PID 1)
scheduler_start()              hand off to the scheduler
```

### Address spaces and the CR3 discipline

Each process gets a private PML4 (`user_address_space_create`) that identity-maps
the kernel low footprint `[0, 4 MiB)`, the framebuffer, the LAPIC MMIO and the
initramfs RAM as **supervisor** pages, plus the user image
(`USER_IMAGE_BASE = 0x400000`), a 1 MiB heap (`USER_HEAP_BASE`) and the stack as
**user** pages.

Because the user CR3 only mirrors `[0, 4 MiB)`, the kernel cannot reach a
process's page-table pages or high data frames by physical (identity) address
while that CR3 is active. So all user-memory access from a syscall — copies
(`user_copy_*`) and `user_range_is_valid` — and all page-table work (process
creation and reaping) switch to the **kernel CR3** first, then reach user memory
by walking the process PML4 + identity-mapped physical frames. These windows
never sleep, so holding the kernel CR3 across them is safe.

A process is a kernel thread (`thread->proc`) whose trampoline calls
`user_enter_ring3(rip, rsp, cr3)`: load user CR3, build an `iretq` frame
(SS=0x1B, RSP, RFLAGS|IF, CS=0x23, RIP) and drop to CPL 3. On every context
switch the scheduler updates **TSS RSP0** and reloads **CR3** when it changes.

### VFS / devfs / processes

`vfs_resolve` matches the longest mount prefix (ramfs `/`, devfs `/dev`) and
delegates the remainder to that filesystem's `lookup`. Open files are refcounted
`(vnode, offset, flags)` tuples referenced by a per-process 32-entry fd table
(fds 0/1/2 → `/dev/console`). `process_spawn[_argv]` loads an HXE1 image from the
VFS into a new address space (kernel CR3 active), builds the argv stack and fd
table, creates the thread and admits it; `process_wait` polls a child to exit,
copies the code and reaps it.

### Syscalls (`int 0x80`)

ABI: `rax`=number, args `rdi, rsi, rdx, r10, r8, r9`, result in `rax`
(negative = `-errno`). `syscall_entry.S` builds a `struct syscall_frame`, calls
`syscall_dispatch`, which indexes a static table (`syscall_table.c`). Calls:
exit, write, read, sleep, getpid, yield, open, close, lseek, readdir, spawn,
wait, getcwd, chdir, uptime, meminfo, ps. Invalid number → `-38` (`-ENOSYS`);
bad pointer → `-14` (`-EFAULT`); the kernel never panics on bad user input.

### init, shell, fault isolation

`/bin/init.hxe` (PID 1) prints the banner, runs `/tests/{syscall,fd,vfs,spawn,
fault}_test.hxe`, then `/bin/shell.hxe`, then exits. The shell drains its
scripted stdin (served by `/dev/console` from the TTY buffer) and spawns the
coreutils. `fault_test` dereferences an unmapped ring-3 address;
`x86_exception_dispatch` sees `CS.RPL == 3`, routes to `user_fault_handle`
(`[OK] User fault isolated`), terminates that process and reschedules — the
kernel survives. The supervisor reaps init and prints
`[OK] Userland foundation tests passed`.
