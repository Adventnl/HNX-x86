# HNX/MyOS Process Model

This document describes the HNX/MyOS process and threading model: how processes
are created from on-disk executables, how the single-core round-robin scheduler
runs kernel threads, how preemption and sleep work, how file-descriptor tables
and process tables are organized, and how a freshly created process makes the
transition into ring 3.

Everything below is grounded in the actual source under `kernel/process/`,
`kernel/sched/`, and the ring-3 entry path in `kernel/user/`. The model is
deliberately minimal ("v0"): one thread per process, no `fork`, spawn-by-path
plus `wait`/`exit`, and cooperative reaping.

## Architecture

The process subsystem sits on top of three lower layers:

1. **Kernel threads** (`kernel/sched/thread.c`). Every schedulable entity is a
   `struct thread`. A thread owns a 16 KiB kernel stack and a saved RSP. Kernel
   threads have `cr3 == 0` (meaning "use the kernel address space") and
   `proc == NULL`.

2. **The scheduler** (`kernel/sched/scheduler.c`). A single-core, FIFO
   round-robin scheduler with a fixed-length time slice. The timer tick decrements
   the slice and sets a need-resched flag; the actual context switch is performed
   after the LAPIC EOI in `scheduler_irq_exit()`.

3. **Processes** (`kernel/process/process.c`). A `struct process` is a PID-bearing
   object that owns a user address space, exactly one kernel thread (the "main
   thread"), a file-descriptor table, and a working directory. The thread's `proc`
   back-pointer is what lets a syscall find "the current process".

The relationship is strictly one-to-one between a process and its main thread.
There is no concept of multiple threads inside a process; concurrency between
user programs comes from multiple processes, each with its own thread, all sharing
the single CPU through the round-robin scheduler.

A process moves through these stages:

```
process_create_argv ──► create_full (kernel CR3 active)
   │  build user_address_space, exec_load HXE1, map stack+heap, build argv
   ▼
process_spawn_argv ──► thread_create_raw(process_thread_entry, p)
   │  set thread->cr3, thread->proc; state = READY
   ▼
scheduler_make_ready(t) ──► (eventually) schedule() ──► context_switch
   │
   ▼
process_thread_entry (ring 0) ──► user_enter_ring3 ──► ring 3 program runs
   │
   ▼  (syscall SYS_EXIT, or a CPU fault)
process_exit_current / process_fault_current ──► thread_exit (state DEAD)
   │
   ▼  (parent)
process_wait ──► reap: destroy address space + fd table, recycle stack, free slot
```

### Address-space split during creation/reaping

Operations that touch arbitrary physical RAM — building page tables, copying an
image into a child's frames, tearing the child's page tables down — run with the
**kernel CR3** active so the full identity map is available. `process_spawn_argv`
and `process_wait` bracket these windows with `user_with_kernel_cr3()` /
`user_restore_cr3()` (declared in `kernel/user/user_copy.h`). The caller must not
sleep inside such a window. A running syscall, by contrast, executes with the
*calling process's* CR3 active so validated user pointers are directly
addressable.

## File map

| File | Role |
| --- | --- |
| `kernel/process/process.h` | `struct process`, `enum process_state`, public API |
| `kernel/process/process.c` | Lifecycle: create, spawn, exit, fault, accessors, argv-block builder, stdio wiring |
| `kernel/process/process_table.h` | Fixed process table API (`PROCESS_MAX = 64`) |
| `kernel/process/process_table.c` | PID allocation, slot lookup, snapshot for `ps` |
| `kernel/process/fd_table.h` | `struct fd_table`, `FD_MAX = 32`, fd helpers |
| `kernel/process/fd_table.c` | fd alloc/install/close/get, refcounted teardown |
| `kernel/process/exec.h` | `exec_load` declaration |
| `kernel/process/exec.c` | Resolve an HXE1 path via the VFS and hand it to the loader |
| `kernel/process/wait.h` | `process_wait` declaration |
| `kernel/process/wait.c` | Block-until-terminated + reap |
| `kernel/sched/thread.h` | `struct thread`, `enum thread_state`, thread API |
| `kernel/sched/thread.c` | Thread creation, kernel-stack recycling, trampoline, exit |
| `kernel/sched/scheduler.h` | Scheduler API + `SCHEDULER_TIME_SLICE_TICKS` |
| `kernel/sched/scheduler.c` | Ready queue, `schedule()`, preemption, CR3/TSS switching |
| `kernel/sched/sleep.h` / `sleep.c` | Tick-based sleep list and wakeup sweep |
| `kernel/sched/idle.h` / `idle.c` | Idle thread (`sti; hlt` loop) |
| `kernel/sched/context_switch.S` | Callee-saved register save/restore + stack swap |
| `kernel/user/user.h` / `user.c` | Ring-3 constants, GDT/TSS validation |
| `kernel/user/user_entry.S` | `user_enter_ring3` (builds the iretq frame to CPL 3) |
| `kernel/user/user_loader.h` / `user_loader.c` | HXE1 format + segment loader |
| `kernel/user/user_address_space.c` | Per-process PML4 build, map/unmap, destroy |
| `kernel/src/kernel.c` | Boot wiring: `process_system_init`, `user_init`, the spawn of PID 1 |

## Data structures

### `struct process` (`kernel/process/process.h`)

```c
struct process {
    uint64_t            pid;
    uint64_t            parent_pid;
    char                name[PROCESS_NAME_MAX];   /* 32 */
    enum process_state  state;
    int64_t             exit_code;

    struct user_address_space *address_space;
    struct thread             *main_thread;
    struct fd_table           *fds;

    char     cwd[PROCESS_CWD_MAX];                /* 256 */

    uint64_t entry_rip;     /* user entry point */
    uint64_t user_rsp;      /* initial user stack pointer (argv block built) */
};
```

`enum process_state` values: `PROCESS_NEW`, `PROCESS_READY`, `PROCESS_RUNNING`,
`PROCESS_SLEEPING`, `PROCESS_WAITING`, `PROCESS_EXITED`, `PROCESS_FAULTED`.

Constants: `PROCESS_NAME_MAX = 32`, `PROCESS_CWD_MAX = 256`,
`PROCESS_MAX_ARGS = 32`, and `PROCESS_MAX = 64` (from `process_table.h`).

`name` is set to `path_basename(path)`; `parent_pid` is the spawning process's PID
(0 if spawned by the kernel supervisor). `entry_rip` and `user_rsp` are captured at
creation and consumed once, in `process_thread_entry`, to enter ring 3.

### `struct thread` (`kernel/sched/thread.h`)

```c
struct thread {
    uint64_t id;
    const char *name;
    enum thread_state state;

    uint64_t *kernel_stack_base;
    uint64_t *kernel_stack_top;
    uint64_t rsp;                  /* saved stack pointer when not running */

    uint64_t wake_tick;            /* absolute tick to wake at (SLEEPING) */

    uint64_t cr3;                  /* 0 == kernel address space */
    void *proc;                    /* back-pointer to struct process, or NULL */

    void (*entry)(void *);
    void *arg;

    struct thread *next;           /* ready-queue / sleep-list link */
};
```

`enum thread_state`: `THREAD_NEW`, `THREAD_READY`, `THREAD_RUNNING`,
`THREAD_SLEEPING`, `THREAD_BLOCKED`, `THREAD_DEAD`.
`THREAD_KERNEL_STACK_SIZE` is 16384 (16 KiB).

The single `next` pointer is shared between two uses: while a thread is in the
ready queue it links the FIFO; while it is `THREAD_SLEEPING` it links the sleep
list. A thread is never in both at once, so the field is safe to reuse.

### `struct fd_table` (`kernel/process/fd_table.h`)

```c
#define FD_MAX 32
struct fd_table {
    struct file *entries[FD_MAX];
};
```

A flat array of `struct file *`. Each slot holds a counted reference to an open
file (`kernel/fs/file.h`). fds 0/1/2 are wired to `/dev/console` at creation.

### Process table (`kernel/process/process_table.c`)

```c
static struct process *g_table[PROCESS_MAX];   /* 64 slots */
static uint64_t g_next_pid;                     /* starts at 1, monotonic */
```

PIDs are monotonically increasing and never reused for the lifetime of the boot
(they are not slot indices). The table is guarded by
`irq_save_flags_and_disable()` / `irq_restore_flags()` for alloc/free.

## Key APIs

### Process lifecycle (`process.h`)

- `void process_system_init(void)` — calls `process_table_init()`; wired from
  `kernel_main` in `kernel/src/kernel.c`.
- `struct process *process_create(const char *path)` /
  `process_create_argv(path, argc, argv)` — build a process (load image, build
  address space, stack, heap, fd table). State `PROCESS_NEW`; not yet scheduled.
- `int process_spawn(const char *path)` / `process_spawn_argv(...)` — create **and**
  admit to the scheduler. Returns the new PID, or a negative errno (`-SYS_ENOENT`
  if creation fails, `-SYS_ENOMEM` if the thread cannot be created).
- `int process_wait(uint64_t pid, int64_t *exit_code)` — block until child `pid`
  terminates, reap it, return the PID.
- `void process_exit_current(int64_t code)` / `process_fault_current(int64_t code)`
  — mark the current process EXITED/FAULTED and `thread_exit()`. Both are
  `noreturn`.
- Accessors: `process_current`, `process_current_pid`, `process_by_pid`,
  `process_address_space`, `process_fds`, `process_cwd`.
- `int process_snapshot(struct sys_ps_entry *out, int max)` — fill `ps` entries
  (delegates to `process_table_snapshot`).

### Creation internals (`process.c`)

- `build_user_stack(space, argc, argv)` — writes argument strings, then a SysV-ish
  block `[argc][argv0..][NULL]` near `USER_STACK_TOP`. The final RSP is 16-byte
  aligned (with an 8-byte pad inserted when `argc + 2` is odd). Returns the initial
  user RSP (pointing at `argc`), or 0 on failure. All writes go through
  `user_copy_to_space`.
- `wire_stdio(p)` — resolves `/dev/console`, allocates one `struct file` opened
  `O_RDWR`, and installs it at fds 0/1/2 with `file_ref` bumping the count for the
  two extra slots.
- `create_full(path, argc, argv)` — the shared worker (assumes kernel CR3 active):
  create address space → `exec_load` → map stack and initial heap → build argv
  block → allocate a process-table slot → fill fields → create fd table → wire
  stdio.
- `process_thread_entry(void *arg)` — the ring-0 trampoline. Sets the process state
  to `PROCESS_RUNNING` and calls
  `user_enter_ring3(p->entry_rip, p->user_rsp, user_address_space_cr3(p->address_space))`.

### Thread API (`thread.h`)

- `thread_create_raw(name, entry, arg)` — allocate a thread + a 16 KiB kernel stack
  (from the recycle pool if available) and plant the initial context-switch frame.
  Does **not** admit it to the ready queue.
- `thread_create(...)` — `thread_create_raw` plus `scheduler_make_ready`.
- `thread_reap(thread)` — recycle a dead thread's kernel stack into the free pool.
- `thread_exit()` — `cli`, mark `THREAD_DEAD`, `scheduler_reschedule()`; `noreturn`.
- `thread_current()` / `thread_current_id()`.
- `context_switch(uint64_t *old_rsp, uint64_t new_rsp)` (asm).

### Scheduler API (`scheduler.h`)

- `scheduler_init()` / `scheduler_start()` (the latter is `noreturn`).
- `scheduler_yield()` — voluntary reschedule (saves/restores IF).
- `scheduler_reschedule()` — reschedule with IF already 0 (used by sleep, exit).
- `scheduler_make_ready(t)` — enqueue a thread (interrupt-safe).
- `scheduler_on_timer_tick()` — slice accounting + sleep wakeup (timer IRQ, IF=0).
- `scheduler_irq_exit()` — deferred preemption switch, after EOI.
- Instrumentation: `scheduler_switch_count()`, `scheduler_preempt_count()`.

### Sleep API (`sleep.h`)

- `thread_sleep_ticks(ticks)` / `thread_sleep_ms(ms)`.
- `sleep_wakeup_expired(current_tick)` — sweep the sleep list (called from the
  timer tick).
- `sleep_wakeup_count()` — test instrumentation.

`thread_sleep_ms` converts using `KERNEL_TIMER_HZ` (100 Hz), rounding up and
clamping to a minimum of 1 tick.

## Scheduling, preemption and the time slice

The scheduler is single-core, FIFO round-robin. The ready queue is a singly-linked
list (`g_ready_head` / `g_ready_tail`); `scheduler_make_ready` appends, and
`ready_dequeue` pops from the head.

The core routine `schedule()` (must run with IF=0):

1. Dequeue the next ready thread. If none and the current thread is still
   `THREAD_RUNNING`, reset the slice and return (the only runnable thread keeps
   running). If none and the current thread is not running, fall back to the idle
   thread.
2. If the previous thread was `THREAD_RUNNING` and is not the idle thread, append
   it to the back of the ready queue (this is the round-robin step).
3. If `next == prev`, just refresh the running state and slice.
4. Otherwise: switch the current pointer, point `TSS.RSP0` at the incoming
   thread's kernel stack top (`tss_set_rsp0`), reload CR3 only if it actually
   changes (`thread_cr3(next) != thread_cr3(prev)`), bump `g_switch_count`, then
   `context_switch(&prev->rsp, next->rsp)`.

`thread_cr3(t)` returns `t->cr3` if non-zero, else `g_kernel_cr3` (captured at
`scheduler_init` from `vmm_kernel_pml4()`). Because every user address space
mirrors the kernel low footprint and both kernel stacks stay mapped under both
CR3s, loading CR3 *before* the stack swap in `schedule()` is safe.

### Time slice and preemption flow

`SCHEDULER_TIME_SLICE_TICKS` is 5; at the 100 Hz tick that is a 50 ms quantum
(per `scheduler.h`). The preemption path is split between IRQ context and a
post-EOI hook to keep the IRQ handler short and the context switch atomic:

1. `scheduler_on_timer_tick()` runs in the timer IRQ (IF=0). It calls
   `sleep_wakeup_expired(kernel_ticks())`, decrements `g_slice`, and when the slice
   hits 0 sets the volatile `g_need_resched` flag.
2. After the LAPIC EOI, `irq_dispatch` calls `scheduler_irq_exit()`. If
   `g_need_resched` is set, it clears the flag, bumps `g_preempt_count` (when there
   is another ready thread), and calls `schedule()`.

A preempted thread's full register state lives on its own kernel stack: the iretq
frame and GPRs come from `irq_stubs.S`, and the callee-saved registers come from
`context_switch`. Nothing of the preempted thread is stored in the scheduler; it
is simply resumed where it left off when next scheduled.

### Cooperative paths

- `scheduler_yield()` brackets `schedule()` with `irq_save_flags_and_disable()` /
  `irq_restore_flags()` and is exposed to ring 3 as `SYS_YIELD`.
- `thread_sleep_ticks` sets `wake_tick`, marks `THREAD_SLEEPING`, links onto
  `g_sleep_head`, and `scheduler_reschedule()`s. The thread resumes from inside
  `thread_sleep_ticks` once the timer sweep re-admits it.

### The idle thread

Created by `scheduler_init` via `thread_create_raw("idle", idle_thread_entry, NULL)`
and never placed in the ready queue. It is the implicit fallback when nothing else
is runnable. Its body is an `sti; hlt` loop (`idle.c`), so the CPU sleeps until the
next interrupt (typically the timer), which can then preempt to a newly woken
thread.

### Context switch mechanics (`context_switch.S`)

`context_switch` pushes the callee-saved registers (`rbp`, `rbx`, `r12`–`r15`),
stores RSP into `*old_rsp`, loads `new_rsp`, pops the same registers, and `ret`s.
Caller-saved registers are not preserved (the C caller already treats them as
clobbered). A brand-new thread's initial frame is built by `thread_create_raw` so
that the `ret` lands in `thread_trampoline`. The trampoline does `sti`, calls
`entry(arg)`, and `thread_exit()`s if the entry function ever returns.

## Process table and PIDs

`process_table_alloc()` scans `g_table` for a free slot (IRQ-disabled), `kcalloc`s
a zeroed `struct process`, assigns `pid = g_next_pid++`, and stores it. Returns
`NULL` if all 64 slots are full. `process_table_free()` clears the slot then
`kfree`s. `process_table_get(pid)` is a linear scan by PID.
`process_table_snapshot()` copies live entries into `struct sys_ps_entry`
(pid, parent_pid, state, name) for the `ps` syscall.

## File-descriptor tables

`fd_table_create()` `kcalloc`s a zeroed table. `fd_table_destroy()` `file_unref`s
every occupant then frees the table. `fd_alloc()` installs a file in the lowest
free slot (returns the fd or `-SYS_EMFILE`). `fd_install_at()` installs at a
specific fd, `file_unref`ing any prior occupant (used for stdio). `fd_close()`
unrefs and clears (returns 0 or `-SYS_EBADF`). `fd_get()` returns the file or
`NULL`.

Open files are refcounted (`struct file` in `kernel/fs/file.h`); the vnode behind a
file belongs to its filesystem and is never freed by the fd layer. fds 0/1/2 share
a single `struct file` (refcount 3 after `wire_stdio`).

## exec / HXE1 loading

`exec_load(cwd, path, space, out_entry)` (`exec.c`):

1. `path_resolve(cwd, path, abs, ...)` to an absolute path.
2. `vfs_resolve(abs)`; reject if missing (`-SYS_ENOENT`) or a directory
   (`-SYS_EISDIR`).
3. Reject images smaller than `sizeof(struct hxe_header)` (32 bytes) with
   `-SYS_ENOEXEC`.
4. `kmalloc` a buffer of the file size and `vnode_read` the whole image; a short
   read is `-SYS_ENOEXEC`.
5. `user_loader_load(space, buf, size, out_entry)`; free the buffer; map a
   loader failure to `-SYS_ENOEXEC`.

### HXE1 format (`user_loader.h`)

HXE1 is a minimal custom load format (not ELF): a header, a segment table, then the
segment bytes. `tools/mkhxe.py` produces it by flattening a linked ELF's PT_LOAD
program headers.

```c
#define HXE_MAGIC   0x31455848U      /* "HXE1" little-endian */
#define HXE_VERSION 1U
#define HXE_SEG_READ 1, HXE_SEG_WRITE 2, HXE_SEG_EXEC 4

struct hxe_header  { magic; version; entry; segment_count; header_size; };
struct hxe_segment { virtual_address; memory_size; file_size; file_offset; flags; };
```

`user_loader_load` (`user_loader.c`) validates the whole image *before* mapping
anything:

- magic == `HXE_MAGIC`, version == `HXE_VERSION`, `header_size >= sizeof(header)`.
- non-zero `segment_count`, and the segment table fits in the image.
- per segment: `file_size <= memory_size`; the virtual range is inside
  `[USER_IMAGE_BASE, USER_TOP)` with no overflow; file bytes are inside the image;
  no page-granular overlap with earlier segments.
- the entry point falls inside an executable (`HXE_SEG_EXEC`) segment.

Then, per segment: `user_map_range` (page-aligned span, `PAGE_WRITABLE` if
`HXE_SEG_WRITE`), which zero-fills (covering BSS), followed by `user_copy_to_space`
of the file bytes. The entry point is written to `*out_entry`.

### User address-space layout (`user.h`)

```
0x0000000000400000  USER_IMAGE_BASE    HXE1 image (code RX, data/bss RW)
0x0000004000000000  USER_HEAP_BASE     bump heap (USER_HEAP_INITIAL = 1 MiB mapped)
USER_STACK_TOP-SIZE .. USER_STACK_TOP  user stack (256 KiB) + argv block
< USER_TOP (0x0000800000000000)        all user virtual addresses
```

`create_full` maps the stack at `USER_STACK_TOP - USER_STACK_SIZE` and the initial
heap at `USER_HEAP_BASE` (1 MiB), both `PAGE_WRITABLE`. Each per-process address
space is a fresh PML4 (`user_address_space_create`) that supervisor-maps the kernel
low footprint, the framebuffer, the LAPIC MMIO window, and the initramfs RAM, then
ORs `PAGE_USER` into only the two intermediate paging levels leading to the user
image (leaves stay supervisor-only, keeping kernel memory inaccessible to ring 3).

## The ring-3 entry path

When the new thread is first scheduled, `context_switch` returns into
`thread_trampoline`, which calls `process_thread_entry`. That sets
`PROCESS_RUNNING` and calls `user_enter_ring3(entry_rip, user_rsp, user_cr3)`.

`user_enter_ring3` (`user_entry.S`):

1. `cli`, load the user CR3 (`mov %rdx, %cr3`).
2. Push an iretq frame: `SS = 0x1B`, `RSP = user_rsp`, `RFLAGS = 0x202` (IF set),
   `CS = 0x23`, `RIP = user_rip`.
3. Zero every GPR so no kernel register state leaks into ring 3.
4. `iretq` — drops to CPL 3 at the program's entry point.

The selectors `0x1B` (`USER_DATA_SELECTOR_RPL3`) and `0x23`
(`USER_CODE_SELECTOR_RPL3`) are the GDT user descriptors with RPL 3
(`GDT_USER_DATA = 0x18`, `GDT_USER_CODE = 0x20`). `user_init` (`user.c`) panics at
boot if these constants do not match the GDT, validating the configuration before
any process runs ("[OK] Ring 3 segments validated", "[OK] Ring 3 entry online").

## wait, exit and zombie handling

A user program ends its life with `SYS_EXIT` (handled by `sys_exit` →
`process_exit_current`) or by faulting in ring 3 (handled by `user_fault_handle` →
`process_fault_current`). Both set the process state (`PROCESS_EXITED` /
`PROCESS_FAULTED`) and the exit code, then call `thread_exit()`, which marks the
thread `THREAD_DEAD` and reschedules away. At this point the process is a "zombie":
its `struct process` and address space still exist, but its thread will never run
again.

`process_wait(pid, exit_code)` (`wait.c`) reaps the zombie:

1. Look up the child; return `-SYS_ECHILD` if it does not exist or is not a child
   of the caller (`child->parent_pid != self->pid`).
2. Cooperatively poll: `while (state != EXITED && state != FAULTED)
   thread_sleep_ms(2)`. The parent sleeps in 2 ms increments while the child runs.
3. Store the child's `exit_code` (if a pointer was supplied).
4. Reap under the kernel CR3: `user_address_space_destroy` (frees the child's page
   tables and user frames), `fd_table_destroy` (unref all files), `thread_reap`
   (recycle the 16 KiB kernel stack), `process_table_free` (free the slot).
5. Return the PID.

A fault is reported by `user_fault_handle` (`kernel/user/user_fault.c`): it prints
a "USER FAULT" diagnostic (vector, name, error code, RIP, RSP, CS, CR2 for page
faults, plus the process name and PID), logs "[OK] User fault isolated", and calls
`process_fault_current(-(int64_t)frame->vector)`. The kernel survives; only the
offending process dies. The fault exit code is the negated trap vector.

The boot supervisor (`user_tests_start`, called from `kernel_main`) spawns
`/bin/init.hxe` as PID 1 and `process_wait`s on it, demonstrating the full
create → run → exit → reap cycle.

## Invariants

- **One thread per process.** `p->main_thread` is the only schedulable entity for a
  process; `t->proc` back-points to it. Kernel threads have `proc == NULL`.
- **`process_current()` is `thread_current()->proc`.** A syscall or fault always
  resolves the current process through the running thread.
- **PIDs are monotonic and never reused.** `g_next_pid` only increments; table
  slots are reused but PIDs are not.
- **`schedule()` runs only with IF=0.** Every entry point ensures this (inherently
  in the IRQ path, or via `irq_save_flags_and_disable` in the yield/sleep paths).
- **The idle thread is never queued.** It is the implicit fallback and is never
  appended to the ready queue.
- **CR3 is reloaded only on a real change.** `schedule()` compares effective CR3s;
  threads sharing the kernel address space avoid a TLB flush.
- **Page-table mutation runs under the kernel CR3.** Creation
  (`process_spawn_argv`) and reaping (`process_wait`) bracket their critical
  sections with `user_with_kernel_cr3` / `user_restore_cr3` and never sleep inside.
- **`thread->next` has a single owner at a time.** Either the ready queue or the
  sleep list, never both.
- **fds 0/1/2 always point at `/dev/console`** for a freshly created process, with
  a shared, refcounted `struct file`.
- **The user image never overlaps the kernel.** `user_address_space_create` panics
  if `kernel_base + kernel_size > USER_IMAGE_BASE`; the loader rejects segments
  below `USER_IMAGE_BASE` or above `USER_TOP`.
- **A ring-3 fault cannot panic the kernel** as long as there is a current process;
  `user_fault_handle` only panics on the (impossible) CPL-3 fault with no process.

## Failure modes

| Condition | Result |
| --- | --- |
| Process table full (>64 live) | `process_table_alloc` returns `NULL`; spawn returns `-SYS_ENOENT` |
| `user_address_space_create` fails (no frames) | `create_full` returns `NULL`; spawn `-SYS_ENOENT` |
| HXE path missing / is a directory | `exec_load` returns `-SYS_ENOENT` / `-SYS_EISDIR` |
| Malformed HXE1 (bad magic, overlap, bad entry) | loader fails; `exec_load` returns `-SYS_ENOEXEC` |
| Image smaller than the header | `exec_load` returns `-SYS_ENOEXEC` |
| Stack/heap mapping fails | `create_full` returns `NULL` |
| argv block does not fit | `build_user_stack` returns 0; `create_full` returns `NULL` |
| `thread_create_raw` fails after create | resources rolled back; spawn returns `-SYS_ENOMEM` |
| `wait` on a non-child / unknown PID | `-SYS_ECHILD` |
| Too many open files (>32) | `fd_alloc` returns `-SYS_EMFILE` |
| Bad fd | `fd_close` / `fd_get` paths return `-SYS_EBADF` / `NULL` |
| Ring-3 CPU exception | process FAULTED with exit code `-vector`; kernel survives |
| `thread_exit` resumes (bug) | `kernel_panic("thread_exit: dead thread resumed")` |
| `scheduler_start` returns (bug) | `kernel_panic("scheduler_start returned to boot context")` |
| Kernel-stack recycle pool full (>64) | the stack is simply not recycled (left to the bump heap) |

## Verification

The process/scheduler stack is exercised by these `make` targets (each boots the
image in QEMU and matches serial markers via `tools/verify_qemu.py`):

- **`make verify-scheduler`** — expects `[OK] Context switch online`,
  `[OK] Scheduler online`, `[OK] Sleep/wakeup online`,
  `[PASS] scheduler round-robin`, `[PASS] sleep/wakeup`,
  `[OK] Scheduler tests passed`.
- **`make verify-preemption`** — expects `[OK] Timer preemption online`,
  `[PASS] timer preemption`.
- **`make verify-process`** — expects `[PASS] spawn_test` (the user-space
  `spawn_test.hxe` exercises spawn + wait + exit-code propagation).
- **`make verify-user-mode`** — expects `[OK] Ring 3 entry online`,
  `[USER] hello from ring 3` (proves the ring-3 entry path).
- **`make verify-user-fault`** — expects `[OK] User fault isolated`,
  `[OK] Userland foundation tests passed` (proves zombie/fault isolation).
- **`make verify-vfs`** — expects `[PASS] vfs_test`, `[PASS] fd_test` (covers fd
  tables).
- **`make verify-prompt4`** rolls up all of the above (plus boot/timer).

Boot-time serial markers emitted along this path include: `[OK] Process table
online` (`process_table_init`), `[OK] File descriptor tables online`,
`[OK] User executable loader online`, `[OK] Ring 3 segments validated`,
`[OK] Ring 3 entry online`, and `[OK] Syscall dispatcher online`.

Build-only checks: **`make verify-user-build`** confirms every `/bin` and `/tests`
HXE program and the initramfs are present; **`make verify-initramfs`** confirms the
archive contains the required programs.

## Future expansion

The model is intentionally a v0 baseline. Natural next steps, several of which the
field names already anticipate:

- **`fork`/`exec` separation.** Today there is only spawn-by-path. A copy-on-write
  `fork` would need page-table cloning in `user_address_space.c` and a second
  thread per process.
- **Multi-threaded processes.** `struct process` owns a single `main_thread`; a
  thread list plus shared address space would generalize it.
- **A real blocking `wait`.** `process_wait` currently polls with `thread_sleep_ms`.
  A wait queue keyed on child termination (using the unused `PROCESS_WAITING`
  state and `THREAD_BLOCKED`) would remove the poll.
- **Priorities / fair scheduling.** The ready queue is a flat FIFO; a multilevel or
  weighted queue would replace `ready_dequeue`/`schedule`.
- **SMP.** The scheduler is explicitly single-core (one ready queue, one
  `g_current`). Per-CPU run queues and locking would be required.
- **`mode` / permissions.** `struct stat.mode` is advisory in v0; `struct process`
  has no uid/gid yet.
- **Heap growth.** Only `USER_HEAP_INITIAL` (1 MiB) of the `USER_HEAP_SIZE` (16 MiB)
  reservation is mapped on spawn; a `brk`/`sbrk` syscall would map the rest on
  demand.
