# MyOS Prompt 2 Specification — Kernel CPU and Memory Foundation

This document extends the completed Stage 1–16 boot pipeline.

Stage 1–16 is already complete:

```text
UEFI firmware
    ↓
custom BOOTX64.EFI
    ↓
load /boot/kernel.elf
    ↓
parse ELF64
    ↓
load kernel segments
    ↓
collect boot_info
    ↓
ExitBootServices
    ↓
jump to kernel
    ↓
kernel prints through framebuffer
```

Prompt 2 starts from that working state.

The goal of Prompt 2 is to turn the tiny framebuffer-printing kernel into a real early kernel foundation with:

```text
CPU descriptor setup
GDT
TSS
IDT
exception handlers
page fault handler
serial logging
physical memory manager
kernel page table ownership
virtual memory helpers
kernel heap
early test framework
```

Prompt 2 must not implement:

```text
timer preemption
APIC timer
scheduler
kernel threads
user mode
syscalls
processes
VFS
initramfs
filesystems
PCI
USB
networking
GUI
SMP
```

Those are later prompts.

## Non-Negotiable Architecture Rules

Do not redesign the completed boot pipeline.

Keep:

```text
x86-64
UEFI
custom BOOTX64.EFI
custom UEFI headers
ELF64 kernel
boot_info pointer passed to kernel
kernel framebuffer console
QEMU + OVMF test path
```

Do not add:

```text
GRUB
Limine
gnu-efi
EDK2
Linux code
BSD code
Windows code
existing kernel code
external libc
host OS runtime dependencies
```

The kernel remains freestanding.

## Prompt 2 Stage Range

Prompt 2 covers these build stages:

```text
17. Kernel architecture folder expansion
18. CPU type and register definitions
19. Early serial COM1 logging
20. Kernel logging router
21. GDT setup
22. TSS setup
23. IDT setup
24. Exception assembly stubs
25. C exception dispatcher
26. Page fault handler
27. CPU halt and panic hardening
28. Physical memory map parser
29. Physical page allocator
30. Kernel virtual memory constants
31. Kernel page table builder
32. Kernel heap allocator
33. Early kernel test framework
34. Documentation and validation
```

The final successful Prompt 2 boot should show both framebuffer and serial logs:

```text
MyOS Kernel 0.0.2
[OK] boot info valid
[OK] serial online
[OK] GDT loaded
[OK] TSS loaded
[OK] IDT loaded
[OK] exceptions online
[OK] physical memory manager online
[OK] page allocator online
[OK] kernel page tables online
[OK] page fault handler online
[OK] kernel heap online
[OK] early kernel tests passed
```

## Required Updated Repository Layout

Keep all existing Stage 1–16 files.

Add these files:

```text
kernel/
├── arch/
│   └── x86_64/
│       ├── cpu.c
│       ├── cpu.h
│       ├── gdt.c
│       ├── gdt.h
│       ├── gdt_load.S
│       ├── tss.c
│       ├── tss.h
│       ├── idt.c
│       ├── idt.h
│       ├── isr.S
│       ├── exceptions.c
│       ├── exceptions.h
│       ├── paging.c
│       ├── paging.h
│       ├── cr.S
│       ├── halt.S
│       ├── port_io.S
│       └── serial.c
│
├── include/
│   ├── log.h
│   ├── panic.h
│   ├── assert.h
│   └── status.h
│
├── memory/
│   ├── pmm.c
│   ├── pmm.h
│   ├── vmm.c
│   ├── vmm.h
│   ├── heap.c
│   ├── heap.h
│   └── memory_layout.h
│
├── tests/
│   ├── early_tests.c
│   └── early_tests.h
│
└── src/
    ├── log.c
    └── assert.c
```

Existing files may be updated:

```text
kernel/src/kernel.c
kernel/src/panic.c
kernel/src/framebuffer_console.c
kernel/include/types.h
kernel/arch/x86_64/linker.ld
Makefile
tools/run_qemu.py
docs/checkpoints.md
docs/boot_process.md
docs/build.md
```

## Build System Requirements

The existing Makefile must be extended to compile:

```text
kernel/arch/x86_64/*.c
kernel/arch/x86_64/*.S
kernel/memory/*.c
kernel/tests/*.c
kernel/src/*.c
```

Do not hard-code every new file manually if a clean wildcard approach is already used.

Keep current kernel flags:

```text
--target=x86_64-unknown-none-elf
-ffreestanding
-fno-stack-protector
-fno-builtin
-fno-pic
-fno-pie
-mno-red-zone
-mcmodel=kernel
-nostdlib
-Wall
-Wextra
```

Add only necessary warning suppressions if unavoidable.

Do not hide real warnings.

`make all`, `make image`, and `make run` must still work.

## Stage 17 — Kernel Architecture Folder Expansion

Create a clean architecture layer for x86-64.

Architecture-specific code goes in:

```text
kernel/arch/x86_64/
```

Architecture-independent memory code goes in:

```text
kernel/memory/
```

Architecture-independent logging, panic, assertions, and tests go in:

```text
kernel/src/
kernel/include/
kernel/tests/
```

Do not put everything into `kernel.c`.

Success:

```text
Code is separated into clear modules.
Kernel still boots after file split.
```

## Stage 18 — CPU Type and Register Definitions

Create:

```text
kernel/arch/x86_64/cpu.h
kernel/arch/x86_64/cpu.c
kernel/arch/x86_64/cr.S
```

Required low-level helpers:

```c
uint64_t x86_read_cr0(void);
uint64_t x86_read_cr2(void);
uint64_t x86_read_cr3(void);
uint64_t x86_read_cr4(void);

void x86_write_cr0(uint64_t value);
void x86_write_cr3(uint64_t value);
void x86_write_cr4(uint64_t value);

uint64_t x86_read_rflags(void);

void x86_lgdt(void *gdtr);
void x86_lidt(void *idtr);
void x86_ltr(uint16_t selector);

void x86_halt(void);
void x86_halt_forever(void);
```

Required CPU info:

```c
struct x86_cpu_info {
    uint64_t cr0;
    uint64_t cr2;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t rflags;
};
```

Add a function:

```c
void x86_cpu_init_early(void);
```

For Prompt 2, this function may only collect/print basic state.

Success output:

```text
[OK] CPU state readable
```

## Stage 19 — Early Serial COM1 Logging

Create:

```text
kernel/arch/x86_64/serial.c
kernel/arch/x86_64/port_io.S
```

Implement port I/O helpers:

```c
uint8_t x86_inb(uint16_t port);
void x86_outb(uint16_t port, uint8_t value);
```

Implement COM1 serial:

```c
void serial_init(void);
int serial_is_ready(void);
void serial_write_char(char c);
void serial_write_string(const char *text);
void serial_write_hex64(uint64_t value);
```

COM1 base:

```text
0x3F8
```

Serial init must configure:

```text
disable interrupts
enable DLAB
baud divisor
8 bits
no parity
one stop bit
FIFO
modem control
```

QEMU must expose serial through:

```text
-serial stdio
```

or keep existing serial setup if already present.

Success:

```text
Serial log appears in terminal while framebuffer log appears in QEMU display.
```

## Stage 20 — Kernel Logging Router

Create:

```text
kernel/include/log.h
kernel/src/log.c
```

The kernel logger must write to both:

```text
framebuffer console
serial console
```

Required functions:

```c
void kernel_log_init(void);
void kernel_log(const char *message);
void kernel_log_line(const char *message);
void kernel_log_ok(const char *message);
void kernel_log_warn(const char *message);
void kernel_log_error(const char *message);
void kernel_log_hex64(const char *label, uint64_t value);
```

Rules:

```text
If framebuffer is not initialized yet, serial still works.
If serial is not initialized yet, framebuffer still works.
Logging must not crash if one output is missing.
```

Success output:

```text
[OK] serial online
[OK] kernel logger online
```

## Stage 21 — GDT Setup

Create:

```text
kernel/arch/x86_64/gdt.h
kernel/arch/x86_64/gdt.c
kernel/arch/x86_64/gdt_load.S
```

Required GDT entries:

```text
null descriptor
kernel code segment
kernel data segment
user data segment
user code segment
TSS low
TSS high
```

Selectors:

```c
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28
```

For now, user segments are installed but not used until a later prompt.

Required function:

```c
void gdt_init(void);
```

`gdt_init` must:

```text
build GDT
load GDTR
reload segment registers
prepare for TSS loading
```

Success output:

```text
[OK] GDT loaded
```

## Stage 22 — TSS Setup

Create:

```text
kernel/arch/x86_64/tss.h
kernel/arch/x86_64/tss.c
```

Define x86-64 TSS structure.

Required fields:

```text
rsp0
ist1
ist2
iomap_base
```

Allocate static stacks:

```text
kernel privilege stack
double fault IST stack
page fault IST stack optional
```

Required function:

```c
void tss_init(void);
```

`tss_init` must:

```text
initialize TSS
set rsp0
set IST stack for double fault
install TSS descriptor in GDT
load TR using ltr
```

Success output:

```text
[OK] TSS loaded
```

## Stage 23 — IDT Setup

Create:

```text
kernel/arch/x86_64/idt.h
kernel/arch/x86_64/idt.c
```

The IDT must support 256 entries.

Required types:

```c
struct idt_entry;
struct idt_pointer;
```

Required functions:

```c
void idt_init(void);
void idt_set_gate(
    uint8_t vector,
    void *handler,
    uint16_t selector,
    uint8_t flags
);
```

Required gate flags:

```text
present
interrupt gate
kernel DPL
```

For now, install exception handlers for vectors:

```text
0–31
```

Success output:

```text
[OK] IDT loaded
```

## Stage 24 — Exception Assembly Stubs

Create:

```text
kernel/arch/x86_64/isr.S
```

Implement stubs for CPU exceptions 0–31.

They must save enough register state to a trap frame and call C dispatcher:

```c
void x86_exception_dispatch(struct x86_trap_frame *frame);
```

Required trap frame fields:

```c
struct x86_trap_frame {
    uint64_t vector;
    uint64_t error_code;

    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;

    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};
```

For exceptions without hardware error code, push a fake error code of `0`.

For exceptions with hardware error code, preserve the real one.

Hardware error-code exceptions include:

```text
8 double fault
10 invalid TSS
11 segment not present
12 stack segment fault
13 general protection fault
14 page fault
17 alignment check
21 control protection
29 VMM communication exception
30 security exception
```

If exact vector support is uncertain, implement at least:

```text
0 divide error
3 breakpoint
6 invalid opcode
8 double fault
13 general protection fault
14 page fault
```

But prefer all 0–31.

Success:

```text
Deliberate invalid instruction or breakpoint enters C exception dispatcher instead of triple faulting.
```

## Stage 25 — C Exception Dispatcher

Create:

```text
kernel/arch/x86_64/exceptions.h
kernel/arch/x86_64/exceptions.c
```

Required functions:

```c
void exceptions_init(void);
void x86_exception_dispatch(struct x86_trap_frame *frame);
const char *x86_exception_name(uint64_t vector);
```

Behavior:

```text
print vector
print exception name
print error code
print RIP
print RSP
print RFLAGS
print CR2 for page fault
panic/halt
```

Prompt 2 does not need recoverable exceptions.

All exceptions may panic.

Success output for deliberate test:

```text
[PANIC] CPU exception
vector: 6
name: invalid opcode
rip: 0x...
```

Do not keep deliberate exception test enabled by default after validation.

## Stage 26 — Page Fault Handler

Page fault handling is part of the exception dispatcher.

For vector 14, print:

```text
page fault address from CR2
error code
present bit
write bit
user bit
reserved bit
instruction-fetch bit if available
RIP
```

Required function:

```c
void page_fault_dump(struct x86_trap_frame *frame);
```

For Prompt 2, page faults panic.

Success:

```text
If a page fault occurs, it prints clear diagnostic information instead of rebooting.
```

## Stage 27 — CPU Halt and Panic Hardening

Update:

```text
kernel/src/panic.c
kernel/include/panic.h
kernel/arch/x86_64/halt.S
```

Panic must:

```text
disable interrupts
print panic banner
print message
halt forever with hlt loop
```

Required functions:

```c
void kernel_panic(const char *message);
void kernel_panic_hex(const char *message, uint64_t value);
void kernel_halt_forever(void);
```

Success:

```text
Kernel panic never returns.
QEMU does not silently reboot.
```

## Stage 28 — Physical Memory Map Parser

Create:

```text
kernel/memory/memory_layout.h
kernel/memory/pmm.h
kernel/memory/pmm.c
```

The PMM must consume the UEFI memory map from:

```c
boot_info->memory_map
```

It must parse UEFI descriptors without relying on bootloader UEFI headers.

Define a kernel-side UEFI memory descriptor struct compatible with what the bootloader passes:

```c
struct efi_memory_descriptor_kernel {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};
```

Use descriptor size from boot_info, not `sizeof`.

Identify usable memory:

```text
EfiConventionalMemory
```

Be conservative with all other types.

Reserve:

```text
physical page 0
kernel image range
boot info range
memory map range
framebuffer range
ACPI reclaim/NVS ranges
UEFI runtime ranges
```

For Prompt 2, it is acceptable to treat BootServicesCode/Data as reserved until a later cleanup stage.

Success output:

```text
[OK] physical memory map parsed
total memory: X MiB
usable memory: X MiB
reserved memory: X MiB
```

## Stage 29 — Physical Page Allocator

Implement a bitmap-based page frame allocator.

Page size:

```text
4096 bytes
```

Required functions:

```c
void pmm_init(const struct boot_info *boot_info);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t physical_address);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_used_pages(void);
void pmm_dump_stats(void);
```

Rules:

```text
Return physical addresses.
Never allocate reserved pages.
Never allocate page 0.
Page addresses must be 4096-byte aligned.
Detect double-free if practical.
Detect invalid free if practical.
```

Bitmap placement:

```text
The PMM bitmap itself must live in usable physical memory.
The pages containing the bitmap must be marked used.
```

Simple approach:

```text
find largest usable region
place bitmap at beginning of that region
mark bitmap pages used
```

Success test:

```text
allocate 16 pages
verify nonzero aligned addresses
free them
free count returns to original
```

Success output:

```text
[OK] page allocator online
```

## Stage 30 — Kernel Virtual Memory Constants

Create:

```text
kernel/memory/memory_layout.h
kernel/memory/vmm.h
kernel/memory/vmm.c
kernel/arch/x86_64/paging.h
```

Define the intended layout, even if Prompt 2 only partially uses it.

Required constants:

```c
#define PAGE_SIZE 4096ULL

#define KERNEL_PHYSICAL_BASE 0x100000ULL

#define KERNEL_HIGHER_HALF_BASE 0xFFFFFFFF80000000ULL
#define KERNEL_DIRECT_MAP_BASE  0xFFFF800000000000ULL
#define KERNEL_HEAP_BASE        0xFFFF900000000000ULL
#define KERNEL_HEAP_SIZE        0x0000000010000000ULL
```

Important:

Prompt 2 may keep the kernel running in its current identity/UEFI-provided mapping if switching to a higher-half mapping is too risky in one pass.

However, Prompt 2 must at least build the page table abstraction and own page tables.

Preferred outcome:

```text
Kernel installs its own PML4.
Identity maps early kernel region.
Maps framebuffer.
Maps required boot structures.
Adds direct map for available physical memory.
Keeps current kernel execution stable.
```

Higher-half execution can be deferred only if clearly documented.

## Stage 31 — Kernel Page Table Builder

Create/implement:

```text
kernel/arch/x86_64/paging.c
kernel/arch/x86_64/paging.h
kernel/memory/vmm.c
kernel/memory/vmm.h
```

Required page table functions:

```c
void vmm_init(const struct boot_info *boot_info);
int vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int vmm_unmap_page(uint64_t virtual_address);
uint64_t vmm_get_physical(uint64_t virtual_address);
void vmm_load_kernel_address_space(void);
```

Required x86-64 page flags:

```c
#define PAGE_PRESENT
#define PAGE_WRITABLE
#define PAGE_USER
#define PAGE_WRITE_THROUGH
#define PAGE_CACHE_DISABLE
#define PAGE_ACCESSED
#define PAGE_DIRTY
#define PAGE_HUGE
#define PAGE_GLOBAL
#define PAGE_NO_EXECUTE
```

Required maps:

```text
kernel code/data/bss
kernel stack
framebuffer MMIO range
boot_info page
UEFI memory map pages
PMM bitmap pages
low identity area needed for current execution
```

If direct map is implemented:

```text
physical 0 → KERNEL_DIRECT_MAP_BASE + 0
```

Required safety:

```text
do not unmap currently executing code
do not unmap current stack
do not unmap framebuffer before console replacement is ready
```

Success output:

```text
[OK] kernel page tables built
[OK] kernel CR3 loaded
```

If loading custom CR3 is deferred, output must say:

```text
[WARN] custom CR3 built but not loaded
```

But the preferred acceptance is custom CR3 loaded.

## Stage 32 — Kernel Heap Allocator

Create:

```text
kernel/memory/heap.h
kernel/memory/heap.c
```

Implement a simple early heap.

The heap may be:

```text
bump allocator first
```

But it must have this interface:

```c
void heap_init(void);
void *kmalloc(uint64_t size);
void *kcalloc(uint64_t count, uint64_t size);
void kfree(void *ptr);
void heap_dump_stats(void);
```

For Prompt 2, `kfree` may be a no-op if documented as early bump allocator behavior.

However, the public API must exist so later stages can replace the implementation.

Heap source options:

```text
use a statically reserved kernel heap array
or use PMM pages mapped into kernel heap virtual range
```

Preferred:

```text
use PMM pages for heap backing if VMM is active
```

Fallback allowed:

```text
static early heap region in .bss
```

Success test:

```text
kmalloc 64 bytes
kmalloc 4096 bytes
kcalloc array
verify zeroed memory
```

Success output:

```text
[OK] kernel heap online
```

## Stage 33 — Early Kernel Test Framework

Create:

```text
kernel/tests/early_tests.h
kernel/tests/early_tests.c
```

Required tests:

```c
void early_tests_run(void);
```

Test categories:

```text
boot_info validation
string/memory helpers
serial/log smoke test
GDT/IDT installed flag checks
PMM allocate/free
VMM map/translate if safe
heap allocation
```

Tests must print:

```text
[TEST] name
[PASS] name
```

If a required test fails:

```text
[PANIC] early test failed: name
```

Success output:

```text
[OK] early kernel tests passed
```

## Stage 34 — Documentation and Validation

Update:

```text
docs/checkpoints.md
docs/boot_process.md
docs/build.md
README.md
```

Add Prompt 2 checkpoint:

```text
Prompt 2 complete when:
- make clean succeeds
- make all succeeds
- make image succeeds
- make run boots QEMU
- bootloader still reaches kernel
- kernel initializes serial
- kernel loads GDT/TSS/IDT
- exceptions are installed
- PMM parses UEFI memory map
- page allocator passes allocate/free tests
- kernel page table code builds and either loads CR3 or documents why loading is deferred
- page fault handler prints diagnostics
- heap allocator passes smoke tests
- QEMU does not reset or triple fault
```

## Required Final Boot Log

The final boot log should include:

```text
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

If CR3 loading is not safe yet, the only acceptable alternate line is:

```text
[WARN] Custom CR3 built but not loaded
```

But still implement all page table construction code.

## Prompt 2 Acceptance Criteria

Prompt 2 is complete only when:

```bash
make clean
make all
make image
make run
```

works and QEMU shows the Stage 1–16 bootloader still functioning plus the Stage 2 kernel foundation logs.

Also verify:

```bash
make debug
```

still launches QEMU paused with GDB stub.

Final response must include:

```text
1. Files created/modified
2. make all result
3. make image result
4. make run result
5. Whether custom CR3 is loaded or deferred
6. Any missing host dependencies
7. Exact next milestone after Prompt 2
```

## Exact Next Milestone After Prompt 2

After Prompt 2, the next milestone is Prompt 3:

```text
Interrupt controller and scheduler foundation:
PIC disable hardening, Local APIC discovery/init, timer source, IRQ routing, kernel threads, context switching, round-robin scheduler, sleep/wakeup, timer preemption.
```
