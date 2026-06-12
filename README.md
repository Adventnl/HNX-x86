# MyOS Prompt 3 Specification — Interrupt Controller and Scheduler Foundation

Prompt 1, Prompt 2, and Prompt 2.5 are complete.

Current verified state:

```text
UEFI → custom BOOTX64.EFI → ELF64 kernel → ExitBootServices → kernel framebuffer + serial log
```

Already implemented and verified:

```text
custom UEFI bootloader
ELF64 kernel loading
boot_info handoff
framebuffer console
COM1 serial logging
GDT
TSS
IDT
exception handlers
page fault diagnostics
panic/halt hardening
physical memory manager
bitmap page allocator
kernel page tables
custom CR3 loaded and validated
kernel heap
early tests
headless QEMU verification targets
```

Prompt 3 goal:

```text
Turn the early single-flow kernel into an interrupt-driven kernel with timer interrupts, IRQ dispatch, kernel threads, context switching, sleep/wakeup, and a preemptive round-robin scheduler.
```

Prompt 3 does **not** implement:

```text
user mode
syscalls
processes
ELF user programs
VFS
initramfs
filesystems
PCI storage drivers
USB
networking
GUI
SMP/multicore scheduling
permissions
signals
```

Those are later prompts.

## Non-Negotiable Rules

Do not redesign existing architecture.

Keep:

```text
x86-64
UEFI
custom BOOTX64.EFI
custom UEFI headers
ELF64 kernel
boot_info handoff
ExitBootServices
framebuffer console
serial logger
custom GDT/TSS/IDT
custom PMM/VMM/heap
QEMU + OVMF validation
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
external kernel code
external scheduler code
external libc
```

## Prompt 3 Stage Range

Prompt 3 covers:

```text
35. Interrupt subsystem structure
36. Legacy PIC disable hardening
37. ACPI MADT parser
38. Local APIC discovery
39. Local APIC enable
40. Local APIC EOI support
41. IRQ dispatcher
42. IRQ assembly stubs
43. PIT timer driver
44. Local APIC timer driver
45. Timer abstraction
46. Kernel tick counter
47. Kernel thread structure
48. Kernel context switch assembly
49. Ready queue
50. Round-robin scheduler
51. Idle thread
52. Sleep/wakeup system
53. Timer preemption
54. Scheduler tests and verification
```

Final expected boot log:

```text
MyOS Kernel 0.0.3
[OK] Kernel entered
[OK] Boot info magic valid
[OK] Framebuffer console online
[OK] Serial online
[OK] GDT loaded
[OK] TSS loaded
[OK] IDT loaded
[OK] Exceptions online
[OK] Physical memory manager online
[OK] Kernel CR3 loaded
[OK] Kernel heap online
[OK] Prompt 2.5 baseline verification passed
[OK] Legacy PIC disabled
[OK] ACPI MADT parsed
[OK] Local APIC discovered
[OK] Local APIC enabled
[OK] IRQ dispatcher online
[OK] PIT timer online
[OK] Local APIC timer online
[OK] Kernel tick online
[OK] Context switch online
[OK] Scheduler online
[OK] Sleep/wakeup online
[OK] Timer preemption online
[OK] Scheduler tests passed
```

## Required New Files

Add:

```text
kernel/arch/x86_64/
├── pic.c
├── pic.h
├── apic.c
├── apic.h
├── madt.c
├── madt.h
├── irq.c
├── irq.h
├── irq_stubs.S
├── pit.c
├── pit.h
├── lapic_timer.c
├── lapic_timer.h
└── timer.c
└── timer.h

kernel/sched/
├── thread.c
├── thread.h
├── context_switch.S
├── scheduler.c
├── scheduler.h
├── sleep.c
├── sleep.h
├── idle.c
└── idle.h

kernel/tests/
├── scheduler_tests.c
└── scheduler_tests.h
```

Modify as needed:

```text
Makefile
kernel/src/kernel.c
kernel/src/log.c
kernel/src/panic.c
kernel/arch/x86_64/idt.c
kernel/arch/x86_64/idt.h
kernel/arch/x86_64/exceptions.c
kernel/arch/x86_64/paging.c
kernel/include/types.h
kernel/include/status.h
kernel/tests/early_tests.c
tools/verify_qemu.py
docs/checkpoints.md
docs/boot_process.md
docs/build.md
README.md
```

## Build System Requirements

Extend the Makefile to compile:

```text
kernel/sched/*.c
kernel/sched/*.S
new kernel/arch/x86_64/*.c
new kernel/arch/x86_64/*.S
```

Add verification targets:

```bash
make verify-interrupts
make verify-timer
make verify-scheduler
make verify-preemption
make verify-prompt3
```

Existing verification targets must continue to pass:

```bash
make verify-boot
make verify-exception
make verify-pagefault
make verify-qemu-matrix
```

Prompt 3 is not complete if Prompt 2.5 verification regresses.

## Stage 35 — Interrupt Subsystem Structure

Create an IRQ subsystem separate from CPU exceptions.

Exceptions are vectors 0–31.

Hardware IRQs should use vectors starting at:

```c
#define IRQ_BASE_VECTOR 0x20
```

Recommended vectors:

```text
0x20 timer
0x21 keyboard later
0x22 cascade/reserved
0x30 local APIC timer if separated
0xF0 spurious APIC vector
```

Required types:

```c
typedef void (*irq_handler_t)(uint8_t vector, void *context);

struct irq_context {
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

Required functions:

```c
void irq_init(void);
void irq_register_handler(uint8_t vector, irq_handler_t handler, void *context);
void irq_unregister_handler(uint8_t vector);
void irq_dispatch(struct irq_context *context);
void irq_enable(void);
void irq_disable(void);
uint64_t irq_save_flags_and_disable(void);
void irq_restore_flags(uint64_t flags);
```

Success:

```text
[OK] IRQ dispatcher online
```

## Stage 36 — Legacy PIC Disable Hardening

Create:

```text
kernel/arch/x86_64/pic.c
kernel/arch/x86_64/pic.h
```

Required functions:

```c
void pic_remap(uint8_t master_offset, uint8_t slave_offset);
void pic_mask_all(void);
void pic_disable(void);
void pic_send_eoi(uint8_t irq);
```

For APIC mode, the PIC must be masked and disabled.

Required behavior:

```text
remap PIC away from CPU exception vectors
mask all IRQ lines
disable legacy PIC path
do not rely on PIC for final timer preemption
```

Success:

```text
[OK] Legacy PIC disabled
```

## Stage 37 — ACPI MADT Parser

Create:

```text
kernel/arch/x86_64/madt.c
kernel/arch/x86_64/madt.h
```

Use `boot_info->rsdp_address`.

Parse:

```text
RSDP
XSDT preferred
RSDT fallback
MADT / APIC table
```

Required MADT records:

```text
type 0: Processor Local APIC
type 1: I/O APIC
type 2: Interrupt Source Override
type 4: Local APIC NMI
type 5: Local APIC Address Override
```

Required output structure:

```c
struct madt_info {
    uint64_t local_apic_physical_base;
    uint32_t local_apic_count;

    uint64_t io_apic_physical_base;
    uint32_t io_apic_id;
    uint32_t global_system_interrupt_base;

    uint8_t has_legacy_irq0_override;
    uint32_t irq0_gsi;
    uint16_t irq0_flags;
};
```

Required functions:

```c
int madt_init(uint64_t rsdp_address);
const struct madt_info *madt_get_info(void);
void madt_dump_info(void);
```

Success:

```text
[OK] ACPI MADT parsed
```

If MADT is missing, panic with clear diagnostic. QEMU should provide MADT.

## Stage 38 — Local APIC Discovery

Create:

```text
kernel/arch/x86_64/apic.c
kernel/arch/x86_64/apic.h
```

Required functions:

```c
int lapic_discover(void);
uint64_t lapic_physical_base(void);
uint64_t lapic_virtual_base(void);
uint32_t lapic_read(uint32_t offset);
void lapic_write(uint32_t offset, uint32_t value);
```

Use MADT base first.

Also read APIC base MSR if MSR helpers already exist or add:

```c
uint64_t x86_read_msr(uint32_t msr);
void x86_write_msr(uint32_t msr, uint64_t value);
```

APIC base MSR:

```text
0x1B
```

Required mapping:

```text
Map Local APIC MMIO page through VMM.
Use cache-disable / write-through flags.
```

Success:

```text
[OK] Local APIC discovered
```

## Stage 39 — Local APIC Enable

Required function:

```c
void lapic_enable(void);
```

Must:

```text
set APIC global enable bit in IA32_APIC_BASE MSR if needed
set spurious interrupt vector register
enable APIC software bit
```

Use spurious vector:

```c
#define LAPIC_SPURIOUS_VECTOR 0xF0
```

Success:

```text
[OK] Local APIC enabled
```

## Stage 40 — Local APIC EOI Support

Required function:

```c
void lapic_send_eoi(void);
```

Called after timer IRQ handling.

Do not send EOI for CPU exceptions.

Only send EOI for hardware interrupts.

Success:

```text
EOI path exists and timer interrupt does not wedge after first tick.
```

## Stage 41 — IRQ Dispatcher

`irq_dispatch` must:

```text
look up registered handler for vector
call handler
send EOI for APIC hardware interrupt vectors
track interrupt counts
handle unregistered IRQs with warning
```

Required functions:

```c
uint64_t irq_count_for_vector(uint8_t vector);
void irq_dump_counts(void);
```

Success:

```text
timer vector count increases during boot
```

## Stage 42 — IRQ Assembly Stubs

Create:

```text
kernel/arch/x86_64/irq_stubs.S
```

Implement stubs for at least:

```text
0x20–0x2F
0x30
0xF0
```

Each stub must:

```text
push vector
push fake error code 0
save general-purpose registers
build irq_context
call irq_dispatch
restore registers
iretq
```

These are interrupt gates.

Do not confuse IRQ stubs with exception stubs.

Success:

```text
hardware timer IRQ enters irq_dispatch
```

## Stage 43 — PIT Timer Driver

Create:

```text
kernel/arch/x86_64/pit.c
kernel/arch/x86_64/pit.h
```

PIT base frequency:

```c
#define PIT_BASE_FREQUENCY 1193182
```

Required functions:

```c
void pit_init_periodic(uint32_t hz);
void pit_stop(void);
uint64_t pit_ticks(void);
```

Use PIT channel 0.

PIT may be used for:

```text
fallback periodic timer
calibrating local APIC timer
```

Success:

```text
[OK] PIT timer online
```

## Stage 44 — Local APIC Timer Driver

Create:

```text
kernel/arch/x86_64/lapic_timer.c
kernel/arch/x86_64/lapic_timer.h
```

Required functions:

```c
void lapic_timer_init(uint32_t hz);
void lapic_timer_stop(void);
uint64_t lapic_timer_ticks(void);
```

Recommended:

```text
Use PIT to calibrate APIC timer if feasible.
Fallback to conservative initial count if calibration is not stable yet.
```

Required APIC timer vector:

```c
#define LAPIC_TIMER_VECTOR 0x30
```

Timer frequency target:

```c
#define KERNEL_TIMER_HZ 100
```

Success:

```text
[OK] Local APIC timer online
```

If APIC timer cannot be made reliable in one pass, PIT timer may temporarily drive scheduler, but this must be documented. Preferred result is APIC timer.

## Stage 45 — Timer Abstraction

Create:

```text
kernel/arch/x86_64/timer.c
kernel/arch/x86_64/timer.h
```

Required functions:

```c
void kernel_timer_init(void);
uint64_t kernel_ticks(void);
uint64_t kernel_uptime_ms(void);
void kernel_timer_on_tick(void);
```

`kernel_timer_on_tick` must be called from the active timer IRQ.

Success:

```text
[OK] Kernel tick online
```

## Stage 46 — Kernel Tick Counter

Implement monotonic tick counter:

```c
volatile uint64_t g_kernel_ticks;
```

At 100 Hz:

```text
1 tick = 10 ms
```

Required behavior:

```text
tick count increases only from timer interrupt
uptime derived from tick count
```

Verification target must prove tick count increases.

## Stage 47 — Kernel Thread Structure

Create:

```text
kernel/sched/thread.c
kernel/sched/thread.h
```

For Prompt 3, these are kernel threads only, not user processes.

Required states:

```c
enum thread_state {
    THREAD_NEW,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_SLEEPING,
    THREAD_BLOCKED,
    THREAD_DEAD
};
```

Required structure:

```c
struct thread {
    uint64_t id;
    const char *name;
    enum thread_state state;

    uint64_t *kernel_stack_base;
    uint64_t *kernel_stack_top;
    uint64_t rsp;

    uint64_t wake_tick;

    void (*entry)(void *);
    void *arg;

    struct thread *next;
};
```

Required functions:

```c
void thread_system_init(void);
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);
void thread_destroy(struct thread *thread);
struct thread *thread_current(void);
uint64_t thread_current_id(void);
void thread_exit(void);
```

Use `kmalloc` for thread structures.

Use PMM/VMM/heap for stacks.

Minimum kernel stack size:

```c
#define THREAD_KERNEL_STACK_SIZE 16384
```

Success:

```text
kernel can create at least 3 kernel threads
```

## Stage 48 — Kernel Context Switch Assembly

Create:

```text
kernel/sched/context_switch.S
```

Required function:

```c
void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
```

Must save/restore callee-saved registers:

```text
rbx
rbp
r12
r13
r14
r15
rsp
```

Thread initial stack must be prepared so first switch enters a trampoline:

```c
void thread_trampoline(void);
```

Trampoline calls:

```text
current_thread->entry(current_thread->arg)
thread_exit()
```

Success:

```text
[OK] Context switch online
```

## Stage 49 — Ready Queue

Create ready queue in scheduler.

Required operations:

```c
void scheduler_add_thread(struct thread *thread);
struct thread *scheduler_pick_next(void);
void scheduler_enqueue_ready(struct thread *thread);
```

Queue type can be simple singly-linked list.

Prompt 3 does not need priorities.

## Stage 50 — Round-Robin Scheduler

Create:

```text
kernel/sched/scheduler.c
kernel/sched/scheduler.h
```

Required functions:

```c
void scheduler_init(void);
void scheduler_start(void);
void scheduler_yield(void);
void scheduler_on_timer_tick(void);
struct thread *scheduler_current_thread(void);
uint64_t scheduler_switch_count(void);
```

Rules:

```text
single-core only
round-robin only
no process abstraction
no user mode
no priority
no SMP
```

The scheduler must switch among READY threads.

Success:

```text
[OK] Scheduler online
```

## Stage 51 — Idle Thread

Create:

```text
kernel/sched/idle.c
kernel/sched/idle.h
```

Idle thread behavior:

```text
enable interrupts
hlt loop
```

Required function:

```c
void idle_thread_entry(void *arg);
```

The scheduler must always have an idle thread to run if nothing else is ready.

## Stage 52 — Sleep/Wakeup System

Create:

```text
kernel/sched/sleep.c
kernel/sched/sleep.h
```

Required functions:

```c
void thread_sleep_ticks(uint64_t ticks);
void thread_sleep_ms(uint64_t milliseconds);
void sleep_wakeup_expired(uint64_t current_tick);
```

Rules:

```text
sleeping thread has wake_tick
timer tick checks sleeping list
expired sleeping threads move to ready queue
```

Success:

```text
[OK] Sleep/wakeup online
```

## Stage 53 — Timer Preemption

Timer interrupt must call:

```c
scheduler_on_timer_tick();
```

Required behavior:

```text
tick increments
sleeping threads wake
current thread time slice decreases
when time slice expires, scheduler switches threads
```

Default quantum:

```c
#define SCHEDULER_TIME_SLICE_TICKS 5
```

At 100 Hz, this is about 50 ms.

Success:

```text
[OK] Timer preemption online
```

## Stage 54 — Scheduler Tests and Verification

Create:

```text
kernel/tests/scheduler_tests.c
kernel/tests/scheduler_tests.h
```

Required function:

```c
void scheduler_tests_start(void);
```

Test threads:

```text
thread A increments counter A then yields/sleeps
thread B increments counter B then yields/sleeps
thread C increments counter C then yields/sleeps
```

Required pass condition:

```text
after enough ticks:
counter A > 0
counter B > 0
counter C > 0
scheduler_switch_count > 0
kernel_ticks increased
sleep/wakeup happened at least once
```

Required output:

```text
[TEST] scheduler round-robin
[PASS] scheduler round-robin
[TEST] sleep/wakeup
[PASS] sleep/wakeup
[TEST] timer preemption
[PASS] timer preemption
[OK] Scheduler tests passed
```

## Verification Targets

### `make verify-interrupts`

Pass if log contains:

```text
[OK] Legacy PIC disabled
[OK] ACPI MADT parsed
[OK] Local APIC discovered
[OK] Local APIC enabled
[OK] IRQ dispatcher online
```

### `make verify-timer`

Pass if log contains:

```text
[OK] PIT timer online
[OK] Local APIC timer online
[OK] Kernel tick online
```

And tick count increases.

### `make verify-scheduler`

Pass if log contains:

```text
[OK] Context switch online
[OK] Scheduler online
[OK] Sleep/wakeup online
[OK] Scheduler tests passed
```

### `make verify-preemption`

Pass if log contains:

```text
[OK] Timer preemption online
[PASS] timer preemption
```

### `make verify-prompt3`

Runs:

```text
verify-boot
verify-exception
verify-pagefault
verify-interrupts
verify-timer
verify-scheduler
verify-preemption
```

Prompt 3 is complete only if all pass.

## Boot Order Required in `kernel_main`

Update `kernel_main` to initialize in this order:

```text
validate boot_info enough for framebuffer
framebuffer console init
serial init
kernel logger init
print MyOS Kernel 0.0.3

validate full boot_info
CPU state helpers
GDT
TSS
IDT
exceptions
PMM
VMM
heap
Prompt 2.5 early tests

PIC disable
MADT parse
Local APIC discover
Local APIC enable
IRQ dispatcher
IRQ stubs installed in IDT
PIT init
Local APIC timer init
kernel timer init
thread system init
scheduler init
scheduler tests start
scheduler start
```

Once scheduler starts, control should not return to normal linear boot flow.

If scheduler returns, panic.

## Completion Criteria

Prompt 3 is complete only when:

```bash
make clean
make all
make image
make verify-boot
make verify-exception
make verify-pagefault
make verify-interrupts
make verify-timer
make verify-scheduler
make verify-preemption
make verify-prompt3
make verify-qemu-matrix
make debug
```

succeed or fail with clear `[DEP MISSING]` messages.

Final report must include:

```text
1. Files created/modified
2. Source line count by category
3. make all result
4. make image result
5. verify-boot result
6. verify-exception result
7. verify-pagefault result
8. verify-interrupts result
9. verify-timer result
10. verify-scheduler result
11. verify-preemption result
12. verify-prompt3 result
13. verify-qemu-matrix result
14. Whether APIC timer or PIT fallback drives scheduling
15. Whether context switching is working
16. Whether timer preemption is working
17. Exact next milestone
```

## Exact Next Milestone After Prompt 3

Prompt 4:

```text
User/kernel boundary:
ring 3 transition, syscall entry, syscall table, user address spaces, simple executable format, initramfs, first user program, write/exit/read/sleep/getpid/yield syscalls.
```
