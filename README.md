# MyOS Prompt 4 Specification — User/Kernel Boundary, Syscalls, Initramfs, First User Program

Prompt 1, Prompt 2, Prompt 2.5, and Prompt 3 are complete.

Current verified state:

```text
UEFI → custom BOOTX64.EFI → ELF64 kernel → ExitBootServices → framebuffer + serial kernel
```

Already implemented and verified:

```text
custom UEFI bootloader
ELF64 kernel loading
boot_info handoff
framebuffer console
COM1 serial logging
GDT/TSS/IDT
exception handlers
page fault diagnostics
PMM
VMM
custom CR3
kernel heap
PIT
Local APIC
IRQ dispatcher
timer interrupts
kernel ticks
kernel threads
context switching
round-robin scheduler
sleep/wakeup
timer preemption
scheduler tests
```

Prompt 4 goal:

```text
Implement the first real user/kernel boundary:
ring 3 transition, per-user address spaces, syscall entry, syscall table,
simple executable format, initramfs loading, first user program, and basic
user-space runtime.
```

Prompt 4 does **not** implement:

```text
full process tree
fork
exec replacement semantics
wait
shell
TTY
keyboard input
VFS
persistent filesystem
PCI storage
USB
networking
GUI
SMP
permissions model
signals
dynamic linking
ELF user loader
POSIX compatibility
```

Those are later prompts.

Prompt 4 should produce a boot where the kernel loads `/bin/init.hxe` from initramfs, enters ring 3, runs user code, receives syscalls, prints user output, handles user exit, and preserves all previous kernel verification.

## Scale Philosophy

This OS is expected to eventually grow to millions of lines because of real feature depth:

```text
drivers
filesystems
networking
GUI
userland
developer tools
tests
debuggers
package system
hardware support
```

Do not inflate line count artificially.

Every line should serve a real architectural purpose.

Write compact, reusable, production-style code that could scale to millions of useful lines without becoming 50–80 million lines of duplicated mess.

## Non-Negotiable Rules

Do not redesign existing architecture.

Keep:

```text
x86-64
UEFI
custom BOOTX64.EFI
custom UEFI headers
ELF64 kernel
custom boot_info
ExitBootServices
framebuffer console
serial logger
custom GDT/TSS/IDT
custom PMM/VMM/heap
custom IRQ/timer/scheduler
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
external libc
external syscall ABI
external userspace runtime
```

## Prompt 4 Stage Range

Prompt 4 covers:

```text
55. Bootloader initramfs loading
56. Initramfs archive format and packer
57. Kernel initramfs parser
58. User executable format
59. User program build system
60. User-space runtime layout
61. User address space creation
62. User page mapping helpers
63. Ring 3 GDT/TSS validation
64. User-mode entry path
65. Syscall vector setup
66. Syscall assembly entry
67. Syscall dispatcher
68. Basic syscall table
69. write syscall
70. exit syscall
71. yield syscall
72. sleep syscall
73. getpid syscall
74. read syscall stub
75. User task/thread integration
76. User fault handling
77. First user program
78. User syscall tests
79. Verification targets
80. Documentation
```

Final expected boot log:

```text
MyOS Kernel 0.0.4
[OK] Prompt 3 baseline verification passed
[OK] Initramfs loaded
[OK] Initramfs parsed
[OK] User executable loader online
[OK] User address space created
[OK] User pages mapped
[OK] Syscall vector installed
[OK] Syscall dispatcher online
[OK] Ring 3 entry online
[USER] hello from ring 3
[USER] write syscall OK
[USER] getpid syscall OK
[USER] yield syscall OK
[USER] sleep syscall OK
[USER] read syscall OK
[USER] exit syscall OK
[OK] First user program exited cleanly
[OK] User/kernel boundary tests passed
```

## Required New Directories

Add:

```text
kernel/user/
kernel/initramfs/
user/
user/lib/
user/init/
user/tests/
tools/
```

## Required New Kernel Files

Add:

```text
kernel/initramfs/initramfs.c
kernel/initramfs/initramfs.h

kernel/user/user.h
kernel/user/user.c
kernel/user/user_address_space.c
kernel/user/user_address_space.h
kernel/user/user_loader.c
kernel/user/user_loader.h
kernel/user/user_task.c
kernel/user/user_task.h
kernel/user/user_entry.S
kernel/user/syscall.c
kernel/user/syscall.h
kernel/user/syscall_entry.S
kernel/user/syscall_numbers.h
kernel/user/user_fault.c
kernel/user/user_fault.h

kernel/tests/user_tests.c
kernel/tests/user_tests.h
```

Modify:

```text
Makefile
bootloader/src/efi_main.c
bootloader/src/file.c
shared/include/boot_info.h
kernel/src/kernel.c
kernel/arch/x86_64/gdt.c
kernel/arch/x86_64/gdt.h
kernel/arch/x86_64/tss.c
kernel/arch/x86_64/tss.h
kernel/arch/x86_64/idt.c
kernel/arch/x86_64/idt.h
kernel/arch/x86_64/isr.S
kernel/arch/x86_64/irq_stubs.S
kernel/arch/x86_64/exceptions.c
kernel/arch/x86_64/timer.c
kernel/sched/thread.c
kernel/sched/thread.h
kernel/sched/scheduler.c
kernel/sched/scheduler.h
kernel/sched/context_switch.S
kernel/memory/vmm.c
kernel/memory/vmm.h
kernel/memory/heap.c
kernel/include/types.h
kernel/include/status.h
tools/build_image.py
tools/verify_qemu.py
docs/checkpoints.md
docs/boot_process.md
docs/build.md
README.md
```

## Required New User-Space Files

Add:

```text
user/lib/crt0.S
user/lib/syscall.c
user/lib/syscall.h
user/lib/string.c
user/lib/string.h
user/lib/stdlib.c
user/lib/stdlib.h
user/lib/start.h

user/init/init.c
user/tests/syscall_test.c

user/linker.ld
```

## Required New Tools

Add:

```text
tools/mkinitramfs.py
tools/mkhxe.py
```

`mkinitramfs.py` packs user executables into a simple initramfs archive.

`mkhxe.py` converts a linked user ELF into the custom user executable format.

## Stage 55 — Bootloader Initramfs Loading

The bootloader must load:

```text
\boot\initramfs.hxf
```

alongside:

```text
\boot\kernel.elf
```

Update `boot_info` to pass:

```c
myos_u64 initramfs_base;
myos_u64 initramfs_size;
```

These fields already exist. Prompt 4 must actually fill them.

Bootloader flow becomes:

```text
load kernel.elf
load initramfs.hxf
load kernel ELF segments
fill boot_info.initramfs_base
fill boot_info.initramfs_size
get framebuffer
find ACPI
get memory map
ExitBootServices
jump to kernel
```

Required bootloader log:

```text
[OK] Initramfs file loaded
```

If initramfs is missing, halt with clear error.

## Stage 56 — Initramfs Archive Format

Create a custom archive format called `HXF1`.

Do not use tar, cpio, zip, ext2, FAT, or existing archive formats.

Format:

```c
#define HXF_MAGIC 0x31465848U

struct hxf_header {
    uint32_t magic;        // 'HXF1'
    uint32_t version;      // 1
    uint32_t entry_count;
    uint32_t header_size;
};

struct hxf_entry {
    char path[128];        // null-terminated absolute path
    uint64_t offset;       // offset from archive base to file data
    uint64_t size;
    uint64_t flags;
};
```

Rules:

```text
paths must start with /
entries must be 8-byte aligned
file data must be 8-byte aligned
no compression
no encryption
no directories needed yet
```

Required files in archive:

```text
/bin/init.hxe
/bin/syscall_test.hxe
/etc/banner.txt
```

`tools/mkinitramfs.py` must:

```text
accept output path
accept repeated source:path mappings
write HXF1 archive
print packed file list
fail clearly on missing input
```

Example:

```bash
python3 tools/mkinitramfs.py \
  build/image/initramfs.hxf \
  /bin/init.hxe=build/user/init.hxe \
  /bin/syscall_test.hxe=build/user/syscall_test.hxe \
  /etc/banner.txt=user/init/banner.txt
```

## Stage 57 — Kernel Initramfs Parser

Create:

```text
kernel/initramfs/initramfs.c
kernel/initramfs/initramfs.h
```

Required functions:

```c
void initramfs_init(uint64_t base, uint64_t size);
int initramfs_is_available(void);
const void *initramfs_find(const char *path, uint64_t *out_size);
void initramfs_dump(void);
```

Required validation:

```text
magic
version
entry_count sanity
header bounds
entry data bounds
path null termination
8-byte alignment warning if violated
```

Required boot log:

```text
[OK] Initramfs loaded
[OK] Initramfs parsed
```

## Stage 58 — User Executable Format

Create a custom user executable format called `HXE1`.

Do not use ELF for user programs in Prompt 4.

User ELF may be an intermediate build artifact on the host, but the kernel must load only HXE1.

Format:

```c
#define HXE_MAGIC 0x31455848U

struct hxe_header {
    uint32_t magic;          // 'HXE1'
    uint32_t version;        // 1
    uint64_t entry;
    uint64_t segment_count;
    uint64_t header_size;
};

struct hxe_segment {
    uint64_t virtual_address;
    uint64_t memory_size;
    uint64_t file_size;
    uint64_t file_offset;
    uint64_t flags;
};
```

Segment flags:

```c
#define HXE_SEG_READ  1
#define HXE_SEG_WRITE 2
#define HXE_SEG_EXEC  4
```

Rules:

```text
segments are page-aligned in memory
file_size <= memory_size
zero memory_size - file_size
entry must be inside an executable mapped segment
user virtual addresses must be below USER_TOP
segments must not overlap kernel space
segments must not overlap each other
```

## Stage 59 — User Program Build System

Extend Makefile to build user programs.

User-space compiler flags:

```text
--target=x86_64-unknown-none-elf
-ffreestanding
-fno-stack-protector
-fno-builtin
-fno-pic
-fno-pie
-mno-red-zone
-mno-sse
-mno-mmx
-msoft-float
-nostdlib
-Wall
-Wextra
```

User linker:

```text
ld.lld -T user/linker.ld
```

User virtual base:

```c
#define USER_IMAGE_BASE 0x0000000000400000ULL
```

Outputs:

```text
build/user/init.elf
build/user/init.hxe
build/user/syscall_test.elf
build/user/syscall_test.hxe
build/image/initramfs.hxf
```

New Make targets:

```bash
make user
make initramfs
make verify-user-build
```

`make image` must include:

```text
/boot/kernel.elf
/boot/initramfs.hxf
```

inside the FAT image.

## Stage 60 — User-Space Runtime Layout

User-space code must not use host libc.

Implement:

```text
user/lib/crt0.S
user/lib/syscall.c
user/lib/syscall.h
user/lib/string.c
user/lib/string.h
user/lib/stdlib.c
user/lib/stdlib.h
```

`crt0.S` must:

```text
define _start
set up call to main
call exit syscall with main return value
```

User `main` signature for Prompt 4:

```c
int main(void);
```

No argv/envp yet.

## Stage 61 — User Address Space Creation

Create:

```text
kernel/user/user_address_space.c
kernel/user/user_address_space.h
```

Required layout:

```c
#define USER_IMAGE_BASE  0x0000000000400000ULL
#define USER_STACK_TOP   0x00007FFFFFFFE000ULL
#define USER_STACK_SIZE  0x0000000000020000ULL
#define USER_TOP         0x0000800000000000ULL
```

Required functions:

```c
struct user_address_space;

struct user_address_space *user_address_space_create(void);
void user_address_space_destroy(struct user_address_space *space);
uint64_t user_address_space_cr3(struct user_address_space *space);

int user_map_page(
    struct user_address_space *space,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

int user_map_range(
    struct user_address_space *space,
    uint64_t virtual_address,
    uint64_t size,
    uint64_t flags
);
```

Each user address space must include:

```text
kernel higher mappings required for syscall/interrupt handling
user pages marked PAGE_USER
user code read/execute
user data read/write
user stack read/write
no user access to kernel memory
```

## Stage 62 — User Page Mapping Helpers

Implement safe helpers:

```c
int user_copy_to_space(
    struct user_address_space *space,
    uint64_t user_dst,
    const void *kernel_src,
    uint64_t size
);

int user_zero_in_space(
    struct user_address_space *space,
    uint64_t user_dst,
    uint64_t size
);

int user_range_is_valid(
    struct user_address_space *space,
    uint64_t user_address,
    uint64_t size,
    int require_write
);
```

These are kernel-side helpers for loading executable segments.

Do not trust user pointers.

## Stage 63 — Ring 3 GDT/TSS Validation

Existing GDT already has user segments.

Prompt 4 must verify:

```text
kernel code selector = 0x08
kernel data selector = 0x10
user data selector = 0x18
user code selector = 0x20
TSS selector = 0x28
```

For ring 3, use selectors with RPL 3:

```c
#define USER_DATA_SELECTOR_RPL3 0x1B
#define USER_CODE_SELECTOR_RPL3 0x23
```

TSS must support per-thread kernel stack switching.

Add:

```c
void tss_set_rsp0(uint64_t rsp0);
```

Scheduler/thread switch must update TSS RSP0 for the currently running thread.

Required log:

```text
[OK] Ring 3 segments validated
```

## Stage 64 — User-Mode Entry Path

Create:

```text
kernel/user/user_entry.S
```

Required function:

```c
void user_enter_ring3(
    uint64_t user_rip,
    uint64_t user_rsp,
    uint64_t user_cr3
);
```

Behavior:

```text
disable interrupts
load user CR3
set up iretq frame
SS = user data selector | 3
RSP = user stack pointer
RFLAGS = IF enabled
CS = user code selector | 3
RIP = user entry
iretq
```

This function does not return.

Required log before entering:

```text
[OK] Ring 3 entry online
```

## Stage 65 — Syscall Vector Setup

Use interrupt vector:

```c
#define SYSCALL_VECTOR 0x80
```

Use `int 0x80` for Prompt 4.

Do not use `syscall/sysret` yet.

Reason:

```text
int 0x80 is simpler to validate with the existing IDT/TSS/iretq path.
syscall/sysret can be added later as an optimization.
```

Install vector `0x80` as an IDT trap/interrupt gate with DPL 3.

Required function:

```c
void syscall_init(void);
```

Required log:

```text
[OK] Syscall vector installed
```

## Stage 66 — Syscall Assembly Entry

Create:

```text
kernel/user/syscall_entry.S
```

User syscall ABI:

```text
rax = syscall number
rdi = arg0
rsi = arg1
rdx = arg2
r10 = arg3
r8  = arg4
r9  = arg5
return value in rax
negative values are errors
```

Syscall entry must:

```text
save user register state
build syscall frame
call syscall_dispatch
place return value into saved rax
restore registers
iretq
```

Syscall frame:

```c
struct syscall_frame {
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

## Stage 67 — Syscall Dispatcher

Create:

```text
kernel/user/syscall.c
kernel/user/syscall.h
kernel/user/syscall_numbers.h
```

Required syscall numbers:

```c
#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_SLEEP  3
#define SYS_GETPID 4
#define SYS_YIELD  5
```

Required functions:

```c
void syscall_init(void);
int64_t syscall_dispatch(struct syscall_frame *frame);
```

Invalid syscall:

```text
return -38
```

Use Linux-like numeric error values only as internal convenience; do not claim POSIX compatibility.

Required log:

```text
[OK] Syscall dispatcher online
```

## Stage 68 — Basic Syscall Table

Implement a static syscall table.

Required behavior:

```text
bounds check syscall number
check handler exists
validate user pointers where required
return negative error on invalid input
do not trust user memory
do not kernel panic on bad user syscall
```

## Stage 69 — write syscall

Signature:

```c
int64_t sys_write(uint64_t fd, uint64_t user_buffer, uint64_t length);
```

Supported FDs:

```text
1 stdout
2 stderr
```

Behavior:

```text
validate user buffer readable
copy in chunks from user memory
write to kernel logger/framebuffer/serial
return bytes written
```

Unsupported FD:

```text
return negative error
```

Required user output marker:

```text
[USER] write syscall OK
```

## Stage 70 — exit syscall

Signature:

```c
int64_t sys_exit(uint64_t code);
```

Behavior:

```text
mark current user task exited
store exit code
wake kernel monitor/test thread if needed
schedule away
never return to user code
```

Required marker:

```text
[USER] exit syscall OK
[OK] First user program exited cleanly
```

## Stage 71 — yield syscall

Signature:

```c
int64_t sys_yield(void);
```

Behavior:

```text
call scheduler_yield
return 0 when resumed
```

Required marker:

```text
[USER] yield syscall OK
```

## Stage 72 — sleep syscall

Signature:

```c
int64_t sys_sleep(uint64_t milliseconds);
```

Behavior:

```text
convert ms to ticks
sleep current thread
return 0 when resumed
```

Required marker:

```text
[USER] sleep syscall OK
```

## Stage 73 — getpid syscall

Signature:

```c
int64_t sys_getpid(void);
```

Prompt 4 does not have full processes yet.

Return current user task ID or current thread ID.

Required marker:

```text
[USER] getpid syscall OK
```

## Stage 74 — read syscall stub

Signature:

```c
int64_t sys_read(uint64_t fd, uint64_t user_buffer, uint64_t length);
```

Prompt 4 has no keyboard/TTY yet.

Supported behavior for fd 0:

```text
return 0 for EOF/no input
```

For length 0:

```text
return 0
```

Validate writable user buffer if length > 0.

Required marker:

```text
[USER] read syscall OK
```

Full keyboard-backed read comes in Prompt 5.

## Stage 75 — User Task / Thread Integration

Create:

```text
kernel/user/user_task.c
kernel/user/user_task.h
```

Required structure:

```c
enum user_task_state {
    USER_TASK_NEW,
    USER_TASK_RUNNING,
    USER_TASK_EXITED,
    USER_TASK_FAULTED
};

struct user_task {
    uint64_t id;
    const char *name;
    enum user_task_state state;
    int64_t exit_code;

    struct user_address_space *address_space;
    struct thread *thread;

    uint64_t entry_rip;
    uint64_t user_stack_top;
};
```

Required functions:

```c
struct user_task *user_task_create_from_initramfs(const char *path);
void user_task_start(struct user_task *task);
struct user_task *user_current_task(void);
void user_task_exit_current(int64_t exit_code);
```

Integrate with kernel threads:

```text
each user task has a kernel thread
that thread enters ring 3
syscalls/interrupts use that thread's kernel stack
scheduler updates TSS.rsp0 on thread switch
```

## Stage 76 — User Fault Handling

If a user program causes:

```text
page fault
general protection fault
invalid opcode
```

the kernel should not crash if the fault came from CPL 3.

Behavior:

```text
detect CPL from CS & 3
if CPL == 3:
    mark current user task faulted
    print user fault diagnostic
    terminate user task
    schedule away
else:
    kernel panic as before
```

Required marker for destructive test:

```text
[OK] User fault isolated
```

Do not allow user fault to triple fault or reboot.

## Stage 77 — First User Program

Create:

```text
user/init/init.c
```

It must:

```text
write "[USER] hello from ring 3\n"
call getpid and verify positive/nonzero result
call yield
call sleep
call read with length 0 or fd 0 EOF behavior
write success markers
exit with code 0
```

Expected user output:

```text
[USER] hello from ring 3
[USER] write syscall OK
[USER] getpid syscall OK
[USER] yield syscall OK
[USER] sleep syscall OK
[USER] read syscall OK
[USER] exit syscall OK
```

## Stage 78 — User Syscall Tests

Create:

```text
user/tests/syscall_test.c
kernel/tests/user_tests.c
kernel/tests/user_tests.h
```

`syscall_test.c` should test:

```text
invalid syscall returns error
write bad pointer returns error
read length 0 returns 0
getpid returns stable ID
yield returns
sleep returns after ticks advanced
exit code is captured
```

Kernel `user_tests.c` should:

```text
load /bin/init.hxe
start it
wait/monitor until exit
verify exit code 0
optionally load /bin/syscall_test.hxe
verify exit code 0
print final pass marker
```

Required marker:

```text
[OK] User/kernel boundary tests passed
```

## Stage 79 — Verification Targets

Add Make targets:

```bash
make verify-user-build
make verify-initramfs
make verify-user-mode
make verify-syscalls
make verify-user-fault
make verify-prompt4
```

### verify-user-build

Pass if:

```text
build/user/init.hxe exists
build/user/syscall_test.hxe exists
build/image/initramfs.hxf exists
```

### verify-initramfs

Pass if boot log contains:

```text
[OK] Initramfs file loaded
[OK] Initramfs loaded
[OK] Initramfs parsed
```

### verify-user-mode

Pass if boot log contains:

```text
[OK] Ring 3 entry online
[USER] hello from ring 3
[OK] First user program exited cleanly
```

### verify-syscalls

Pass if boot log contains:

```text
[USER] write syscall OK
[USER] getpid syscall OK
[USER] yield syscall OK
[USER] sleep syscall OK
[USER] read syscall OK
[USER] exit syscall OK
[OK] User/kernel boundary tests passed
```

### verify-user-fault

Build/run a user program or test mode that intentionally performs invalid user memory access.

Pass if:

```text
[OK] User fault isolated
```

and kernel continues running.

### verify-prompt4

Runs:

```text
verify-boot
verify-exception
verify-pagefault
verify-interrupts
verify-timer
verify-scheduler
verify-preemption
verify-user-build
verify-initramfs
verify-user-mode
verify-syscalls
verify-user-fault
```

All must pass.

## Stage 80 — Documentation

Update:

```text
README.md
docs/checkpoints.md
docs/boot_process.md
docs/build.md
```

Document:

```text
Prompt 4 files
initramfs format HXF1
user executable format HXE1
user address space layout
ring 3 entry path
syscall ABI
syscall table
user runtime
read syscall limitation
user fault isolation
verification targets
exact next milestone
```

## Required Final Validation

Prompt 4 is complete only when:

```bash
make clean
make all
make user
make initramfs
make image
make verify-boot
make verify-exception
make verify-pagefault
make verify-interrupts
make verify-timer
make verify-scheduler
make verify-preemption
make verify-user-build
make verify-initramfs
make verify-user-mode
make verify-syscalls
make verify-user-fault
make verify-prompt4
make verify-qemu-matrix
make debug
```

succeed or fail with clear `[DEP MISSING]` messages.

## Final Report Required

Final response must include:

```text
1. Files created/modified
2. Source line count by category
3. make all result
4. make user result
5. make initramfs result
6. make image result
7. verify-boot result
8. verify-exception result
9. verify-pagefault result
10. verify-interrupts result
11. verify-timer result
12. verify-scheduler result
13. verify-preemption result
14. verify-user-build result
15. verify-initramfs result
16. verify-user-mode result
17. verify-syscalls result
18. verify-user-fault result
19. verify-prompt4 result
20. verify-qemu-matrix result
21. Whether ring 3 entry works
22. Whether int 0x80 syscall path works
23. Whether user faults are isolated
24. Exact next milestone
```

## Exact Next Milestone After Prompt 4

Prompt 5:

```text
Process model and interactive userland:
real process table, spawn/exec/wait, file descriptor model, devfs console,
keyboard input, TTY layer, init process, shell, and first core utilities.
```
