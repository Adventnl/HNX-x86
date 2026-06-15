# HNX/MyOS System Call ABI

This document describes the HNX/MyOS system-call interface: the register ABI, the
`int 0x80` entry path, the syscall table and number assignments, how arguments and
user pointers are validated, and the fault-isolation model that keeps a misbehaving
user program from taking down the kernel.

Everything is grounded in `kernel/user/`: `syscall_numbers.h`, `syscall_abi.h`,
`syscall_entry.S`, `syscall.c/.h`, `syscall_table.c/.h`, `user_copy.c/.h`,
`user_fault.c`, and `user_address_space.c`.

## Architecture

MyOS uses the legacy software-interrupt path, **`int 0x80`**, not the
`syscall`/`sysret` fast path. The choice is documented in `syscall.h`. The flow:

```
ring 3:  rax = number; args in rdi, rsi, rdx, r10, r8, r9;  int $0x80
   │  CPU: DPL-3 interrupt gate on vector 0x80 → switch to TSS.RSP0 (kernel stack)
   │       push SS, RSP, RFLAGS, CS, RIP
   ▼
syscall_entry (syscall_entry.S, ring 0, IF=0)
   │  cld; push all GPRs → completes a struct syscall_frame
   │  rdi = &frame; call syscall_dispatch
   ▼
syscall_dispatch (syscall.c)
   │  fn = syscall_table_get(frame->rax);  if (!fn) return -SYS_ENOSYS;
   │  return fn(frame);
   ▼
sys_* handler (syscall.c)
   │  validate user pointers (user_copy_*), do the work, return int64_t
   ▼
syscall_entry: write rax = result into the saved RAX slot; pop GPRs; iretq → ring 3
```

The gate is installed in `syscall_init` (`syscall.c`):

```c
idt_set_gate(SYSCALL_VECTOR, (void *)syscall_entry,
             GDT_KERNEL_CODE, IDT_FLAG_INTERRUPT_GATE_DPL3);
```

`SYSCALL_VECTOR` is `0x80`, `GDT_KERNEL_CODE` is `0x08`, and
`IDT_FLAG_INTERRUPT_GATE_DPL3` is `0xEE` (present, DPL 3, 64-bit interrupt gate).
DPL 3 is what lets ring-3 code raise the interrupt; the *interrupt* gate type
clears IF for the duration so a syscall cannot be nested by an asynchronous IRQ.

## File map

| File | Role |
| --- | --- |
| `kernel/user/syscall_numbers.h` | Syscall numbers, `SYSCALL_VECTOR`, open/seek flags, errno values. Shared with user space via `-Ikernel/user`. |
| `kernel/user/syscall_abi.h` | C-only shared structs for structured syscalls (dirent, stat, meminfo, ps, mounts, devices, hw, etc.) |
| `kernel/user/syscall.h` | `struct syscall_frame`, entry/init/dispatch prototypes |
| `kernel/user/syscall_entry.S` | `int 0x80` assembly entry: build frame, dispatch, return, `iretq` |
| `kernel/user/syscall.c` | The dispatcher and every `sys_*` handler |
| `kernel/user/syscall_table.h` | `syscall_fn` typedef, handler prototypes |
| `kernel/user/syscall_table.c` | The number → handler table (designated initializers) |
| `kernel/user/user_copy.h` / `user_copy.c` | Validated kernel↔user copies, CR3 helpers |
| `kernel/user/user_address_space.c` | `user_range_is_valid`, page-table walk, copy-to/from-space |
| `kernel/user/user_fault.c` | Ring-3 fault isolation |
| `kernel/src/kernel.c` | Boot wiring: `syscall_init`, `user_init` |

## Data structures

### Register frame (`syscall.h`)

`syscall_entry.S` builds this on the kernel stack; the field order matches the push
order and (intentionally) `struct irq_context`:

```c
struct syscall_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, cs, rflags, rsp, ss;   /* pushed by the CPU on int 0x80 */
};
```

`r15` is at the lowest address; `rax` is 14 slots (14*8 bytes) up — which is exactly
where `syscall_entry.S` writes the return value. The five CPU-pushed fields
(`rip`..`ss`) describe the interrupted ring-3 context.

### The register ABI

From `syscall_numbers.h` and `syscall.h`:

| Register | Role |
| --- | --- |
| `rax` | syscall number (in), return value (out) |
| `rdi` | argument 1 |
| `rsi` | argument 2 |
| `rdx` | argument 3 |
| `r10` | argument 4 |
| `r8`  | argument 5 |
| `r9`  | argument 6 |

A negative return value is `-errno`. The handlers read arguments from the frame as
`f->rdi`, `f->rsi`, etc. (Note: the in-tree handlers currently use up to three
arguments; `r10`/`r8`/`r9` are reserved by the ABI for future use.)

### Handler table (`syscall_table.c`)

```c
typedef int64_t (*syscall_fn)(struct syscall_frame *f);
static const syscall_fn g_table[SYS_MAX_NR] = { [SYS_EXIT] = sys_exit, ... };
syscall_fn syscall_table_get(uint64_t nr) {
    if (nr >= SYS_MAX_NR) return (syscall_fn)0;
    return g_table[nr];
}
```

A flat, statically-initialized array indexed by syscall number, bounds-checked by
`syscall_table_get`. Any out-of-range or unpopulated entry yields `NULL`, which the
dispatcher turns into `-SYS_ENOSYS`.

### Shared ABI structs (`syscall_abi.h`)

These use fixed-width fields so the kernel and the freestanding user runtime agree
byte-for-byte. The notable ones:

- `struct sys_dirent { char name[128]; unsigned long long size; unsigned int type; unsigned int _pad; }`
- `struct sys_stat { unsigned long long size; unsigned int type; unsigned int mode; }`
- `struct sys_meminfo { total_pages; free_pages; used_pages; page_size; }`
- `struct sys_ps_entry { pid; parent_pid; unsigned int state; unsigned int _pad; char name[32]; }`
- `struct sys_mount_entry { char path[64]; char fs[16]; }`
- `struct sys_device_entry`, `sys_block_entry`, `sys_usb_entry`, `sys_hw_info`,
  `sys_irq_entry`, `sys_input_event`, `sys_mouse_event`, `sys_msi_entry`.

## Key APIs

### Entry / dispatch (`syscall.h`)

- `void syscall_init(void)` — install the IDT gate on vector `0x80` and log
  `[OK] Syscall vector installed` / `[OK] Syscall dispatcher online`.
- `void syscall_entry(void)` — the asm entry (defined in `syscall_entry.S`).
- `int64_t syscall_dispatch(struct syscall_frame *frame)` — look up and run the
  handler; returns `-SYS_ENOSYS` for an unknown number. Never panics on bad input.

### Validated copies (`user_copy.h`)

- `int user_copy_from_user(void *kdst, uint64_t usrc, uint64_t n)` — copy `n` bytes
  in from user space; returns 0 or `-SYS_EFAULT`.
- `int user_copy_to_user(uint64_t udst, const void *ksrc, uint64_t n)` — copy out;
  0 or `-SYS_EFAULT`.
- `char *user_copy_string_from_user(uint64_t usrc, uint64_t maxlen)` — duplicate a
  NUL-terminated user string into a fresh `kmalloc`'d buffer (caller frees); `NULL`
  on fault or overflow.
- `uint64_t user_with_kernel_cr3(void)` / `void user_restore_cr3(uint64_t saved)` —
  bracket a short, non-blocking critical section that touches arbitrary physical
  RAM with the kernel CR3 active.

### Range validation (`user_address_space.c`)

- `int user_range_is_valid(struct user_address_space *space, uint64_t addr,
  uint64_t size, int require_write)` — returns 1 iff `[addr, addr+size)` is entirely
  mapped, `PAGE_USER`, and (if `require_write`) `PAGE_WRITABLE`.
- `int user_copy_to_space(...)` / `int user_copy_from_space(...)` — page-walking
  copies through the frames' identity addresses (kernel CR3 must be active).

## Syscall numbers

Every number is defined in `kernel/user/syscall_numbers.h`. `SYS_MAX_NR` is 29 —
one past the highest valid number — and is the size of the table and the bound in
`syscall_table_get`.

| # | Name | Handler | Meaning / arguments (rdi, rsi, rdx) |
| --- | --- | --- | --- |
| 0 | `SYS_EXIT` | `sys_exit` | Terminate the current process with code `rdi`. Never returns. |
| 1 | `SYS_WRITE` | `sys_write` | Write `rdx` bytes from user buffer `rsi` to fd `rdi`. Chunked (256 B). Returns bytes written. |
| 2 | `SYS_READ` | `sys_read` | Read up to `rdx` bytes into user buffer `rsi` from fd `rdi` (≤256 per call). Returns bytes read, 0 = EOF. |
| 3 | `SYS_SLEEP` | `sys_sleep` | Sleep `rdi` milliseconds (`thread_sleep_ms`). |
| 4 | `SYS_GETPID` | `sys_getpid` | Return the current PID. |
| 5 | `SYS_YIELD` | `sys_yield` | Voluntarily yield the CPU (`scheduler_yield`). |
| 6 | `SYS_OPEN` | `sys_open` | Open path (user string `rdi`) with flags `rsi`; returns fd. |
| 7 | `SYS_CLOSE` | `sys_close` | Close fd `rdi`. |
| 8 | `SYS_LSEEK` | `sys_lseek` | Seek fd `rdi` by `rsi` with whence `rdx` (`SEEK_SET/CUR/END`). |
| 9 | `SYS_READDIR` | `sys_readdir` | Read one dir entry from fd `rdi` into `struct sys_dirent` at `rsi`. 1 = entry, 0 = end. |
| 10 | `SYS_SPAWN` | `sys_spawn` | Spawn the program at path `rdi` with optional argv `rsi`; returns the child PID. |
| 11 | `SYS_WAIT` | `sys_wait` | Wait for child `rdi`; if `rsi` non-NULL, store the `int64_t` exit code there. |
| 12 | `SYS_GETCWD` | `sys_getcwd` | Copy the cwd into user buffer `rdi` of size `rsi`; returns length. |
| 13 | `SYS_CHDIR` | `sys_chdir` | Change cwd to path `rdi` (must be a directory). |
| 14 | `SYS_UPTIME` | `sys_uptime` | Return uptime in milliseconds (`kernel_uptime_ms`). |
| 15 | `SYS_MEMINFO` | `sys_meminfo` | Fill `struct sys_meminfo` at `rdi`. |
| 16 | `SYS_PS` | `sys_ps` | Fill up to `rsi` `struct sys_ps_entry` at `rdi`; returns count. |
| 17 | `SYS_MKDIR` | `sys_mkdir` | Create directory at path `rdi`. |
| 18 | `SYS_UNLINK` | `sys_unlink` | Remove the file/empty-dir at path `rdi`. |
| 19 | `SYS_STAT` | `sys_stat` | Stat path `rdi` into `struct sys_stat` at `rsi`. |
| 20 | `SYS_MOUNT_INFO` | `sys_mount_info` | Fill up to `rsi` `struct sys_mount_entry` at `rdi`; returns count. |
| 21 | `SYS_DEVICES` | `sys_devices` | Fill up to `rsi` `struct sys_device_entry` at `rdi`; returns count. |
| 22 | `SYS_BLOCKS` | `sys_blocks` | Fill up to `rsi` `struct sys_block_entry` at `rdi`; returns count. |
| 23 | `SYS_USB_DEVICES` | `sys_usb_devices` | Fill up to `rsi` `struct sys_usb_entry` at `rdi`; returns count. |
| 24 | `SYS_HW_INFO` | `sys_hw_info` | Fill `struct sys_hw_info` at `rdi` (counts of PCI funcs, devices, IRQs, etc.). |
| 25 | `SYS_INTERRUPTS` | `sys_interrupts` | Fill up to `rsi` `struct sys_irq_entry` (vectors 0x20–0x4F with non-zero counts) at `rdi`. |
| 26 | `SYS_INPUT_POLL` | `sys_input_poll` | Pop one `struct sys_input_event` into `rdi`; 1 = event, 0 = none. |
| 27 | `SYS_MOUSE_POLL` | `sys_mouse_poll` | Pop one `struct sys_mouse_event` into `rdi`; 1 = event, 0 = none. |
| 28 | `SYS_MSI_INFO` | `sys_msi_info` | Fill up to `rsi` `struct sys_msi_entry` at `rdi`; returns count. |

`SYS_MAX_NR = 29`.

### Open flags and seek whence (`syscall_numbers.h`)

```c
#define SEEK_SET 0   #define SEEK_CUR 1   #define SEEK_END 2

#define O_RDONLY 0x0000   #define O_WRONLY 0x0001   #define O_RDWR 0x0002
#define O_DIRECTORY 0x0010   #define O_CREAT 0x0040   #define O_TRUNC 0x0200
```

### errno values (returned negated)

A subset of POSIX numbers (MyOS does not claim POSIX compatibility; the names just
ease future expansion):

| Name | Value | Used for |
| --- | --- | --- |
| `SYS_EPERM` | 1 | Operation not permitted (e.g. write to read-only fs) |
| `SYS_ENOENT` | 2 | No such file / spawn failed |
| `SYS_ESRCH` | 3 | No such process |
| `SYS_EIO` | 5 | Low-level I/O error |
| `SYS_ENOEXEC` | 8 | Malformed executable |
| `SYS_EBADF` | 9 | Bad file descriptor |
| `SYS_ECHILD` | 10 | Not a child / unknown PID in `wait` |
| `SYS_ENOMEM` | 12 | Out of memory / table full |
| `SYS_EFAULT` | 14 | Bad user pointer |
| `SYS_EEXIST` | 17 | File already exists |
| `SYS_ENOTDIR` | 20 | Not a directory |
| `SYS_EISDIR` | 21 | Is a directory |
| `SYS_EINVAL` | 22 | Invalid argument |
| `SYS_EMFILE` | 24 | Too many open files |
| `SYS_ERANGE` | 34 | Buffer too small (e.g. `getcwd`) |
| `SYS_ENAMETOOLONG` | 36 | Path too long |
| `SYS_ENOSYS` | 38 | Invalid / unimplemented syscall |

Note the two values that are deliberately out of numeric order to keep the POSIX
spellings: `SYS_ENOEXEC` is 8 (listed at the bottom of the header) and `SYS_EEXIST`
is 17.

## The entry path in detail (`syscall_entry.S`)

On entry from ring 3 through the DPL-3 interrupt gate, the CPU has already switched
to the current thread's kernel stack (loaded from `TSS.RSP0` by the scheduler) and
pushed `SS, RSP, RFLAGS, CS, RIP`. `syscall_entry` then:

1. `cld` — clear DF so kernel string ops behave.
2. Push all 15 GPRs in the order `rax, rbx, rcx, rdx, rsi, rdi, rbp, r8, r9, r10,
   r11, r12, r13, r14, r15`. After the pushes, `r15` is at the lowest address and
   the stack image is exactly a `struct syscall_frame`.
3. `mov %rsp, %rdi` — pass the frame pointer; `call syscall_dispatch`.
4. `mov %rax, 14*8(%rsp)` — overwrite the saved `rax` slot with the return value.
5. Pop the GPRs in reverse order, then `iretq` — restoring the ring-3 context. The
   user `RFLAGS` (with IF=1) is restored by `iretq`, re-enabling interrupts in
   ring 3.

Because the gate is an *interrupt* gate, IF stays 0 for the whole call — there is no
syscall nesting and no preemption mid-handler.

## Argument validation and bounds checking

Every handler treats user-supplied pointers as untrusted. A syscall runs with the
*calling process's* CR3 active, so a bad pointer would, if dereferenced naively,
fault in ring 0 and be misreported. Two complementary mechanisms prevent that:

### 1. Validate-then-translate copies (`user_copy.c`)

`user_copy_from_user` / `user_copy_to_user` both:

1. Resolve the current process's address space (`process_address_space`); `NULL` →
   `-SYS_EFAULT`.
2. Switch to the kernel CR3 (`user_with_kernel_cr3`) so the full identity map is
   available — this means neither the user data pages nor the user page-table pages
   need to be mapped in the limited user CR3 mirror.
3. `user_range_is_valid(space, addr, n, require_write)` — confirm the whole range is
   mapped, `PAGE_USER`, and writable (for the copy-to direction).
4. `user_copy_to_space` / `user_copy_from_space` — page-walk the user page tables
   and `memcpy` through each frame's identity address.
5. Restore the previous CR3.

The window is non-blocking, so holding the kernel CR3 across it is safe.

`user_copy_string_from_user` is stricter: it walks one byte at a time, validating
*each* byte's page before translating it, so it never reads past a mapped page. It
only allocates and copies if a terminating NUL is found within `maxlen`; otherwise
it returns `NULL` (overflow). Callers `kfree` the result.

### 2. Range validation (`user_range_is_valid`)

```c
uint64_t end = addr + size;
if (end <= addr || end > USER_TOP) return 0;     /* overflow or out of range */
for (page = PAGE_ALIGN_DOWN(addr); page < end; page += PAGE_SIZE) {
    leaf = walk_leaf(space->pml4, page, &is_2m);
    if (!(leaf & PAGE_PRESENT) || !(leaf & PAGE_USER)) return 0;
    if (require_write && !(leaf & PAGE_WRITABLE)) return 0;
}
```

`USER_TOP` is `0x0000800000000000` (128 TiB, the canonical low half). The overflow
check (`end <= addr`) rejects wrap-around. `walk_leaf` handles 4 KiB, 2 MiB, and
1 GiB leaves. Crucially, the `PAGE_USER` check rejects any address that resolves to
a supervisor mapping — so a user program cannot trick a syscall into reading the
kernel low footprint, the framebuffer, or the LAPIC window, even though those are
mapped in its CR3.

### How handlers apply this

- **Path arguments** use `user_copy_string_from_user(f->rdi, VFS_PATH_MAX)` and free
  the buffer afterward. Example: `sys_open`, `sys_chdir`, `sys_mkdir`, `sys_unlink`,
  `sys_stat`, `sys_spawn`.
- **`sys_write`** copies in through a 256-byte kernel `chunk[]` in a loop, calling
  `vfs_write` per chunk and stopping on a short write; a copy fault returns
  `-SYS_EFAULT` (or the bytes already written).
- **`sys_read`** reads into a kernel `chunk[]` first (≤256 B), then
  `user_copy_to_user`s the result out; a fault is `-SYS_EFAULT`.
- **Structured outputs** (`sys_meminfo`, `sys_stat`, `sys_ps`, `sys_dirent`,
  `sys_mount_info`, `sys_devices`, `sys_blocks`, the Prompt-6 hw/usb/input/msi
  syscalls) build the struct on the kernel stack and `user_copy_to_user` it out,
  returning `-SYS_EFAULT` on a bad destination.
- **`sys_getcwd`** rejects an undersized buffer with `-SYS_ERANGE` *before* copying.
- **`sys_ps` / array syscalls** clamp the requested count to the relevant maximum
  (`PROCESS_MAX`, `vfs_mount_count()`, `device_count()`, etc.) before copying, so a
  large `rsi` cannot overrun the kernel temp buffer or the user buffer.
- **`sys_spawn` argv** is read pointer-by-pointer from `rsi`: each 8-byte pointer is
  copied in (`user_copy_from_user`), then the string it points at is duplicated
  with `user_copy_string_from_user(ptr, 256)`, up to `PROCESS_MAX_ARGS` (32). All
  duplicated strings are freed after the spawn.

## Fault isolation model

There are two distinct "fault" concepts:

1. **Bad syscall arguments.** A handler never panics on user error: every invalid
   pointer, fd, path, or number returns a negative errno. The dispatcher returns
   `-SYS_ENOSYS` for an out-of-range or unimplemented number. This is the
   software-validated path (`user_copy_*`, `user_range_is_valid`).

2. **A CPU exception taken in ring 3.** If user code dereferences an unmapped
   address, executes an illegal instruction, etc., the CPU traps. The exception
   dispatcher detects `CS.RPL == 3` and routes to `user_fault_handle`
   (`user_fault.c`):
   - `cli`, then print a "USER FAULT" block: vector, exception name, error code,
     RIP, RSP, CS, and (for `VEC_PAGE_FAULT`) the faulting address from CR2, plus
     the offending process's name and PID.
   - Log `[OK] User fault isolated`.
   - Call `process_fault_current(-(int64_t)frame->vector)` — mark the process
     `PROCESS_FAULTED`, set the exit code to the negated vector, and `thread_exit()`.

The kernel keeps running; only the faulting process dies, to be reaped later by its
parent's `wait`. The single panic in this path
(`kernel_panic("user fault with no current process")`) covers the impossible case
of a CPL-3 fault with no current process.

The separation between kernel and user memory is enforced structurally by
`user_address_space.c`: a process's PML4 maps the kernel low footprint, framebuffer,
LAPIC, and initramfs as **supervisor** 2 MiB leaves, and only ORs `PAGE_USER` into
the two intermediate paging levels that lead to the user image. Ring-3 code can
neither read nor write any supervisor leaf, and `user_range_is_valid` refuses to
let a syscall touch one on the program's behalf.

## Invariants

- **The syscall number is bounds-checked** in `syscall_table_get` (`nr >= SYS_MAX_NR`
  → `NULL` → `-SYS_ENOSYS`). The table has exactly `SYS_MAX_NR` (29) entries.
- **No handler panics on user input.** Bad input always yields a negative errno.
- **No unvalidated user pointer is ever dereferenced.** Every user access goes
  through `user_range_is_valid` + a page-walking copy, with the kernel CR3 active.
- **The kernel↔user copy window is non-blocking.** Holding the kernel CR3 across it
  is safe precisely because the code never sleeps inside it.
- **Supervisor mappings are unreachable from ring 3.** The `PAGE_USER` checks (both
  in the page tables and in `user_range_is_valid`) guarantee it.
- **IF stays 0 for the whole syscall.** The interrupt gate type guarantees no
  nesting; `iretq` restores the user RFLAGS (IF=1).
- **The return value lands in the user's `rax`.** `syscall_entry.S` writes
  `14*8(%rsp)`, exactly the `rax` slot of `struct syscall_frame`.
- **The ABI cannot drift.** `syscall_numbers.h` and `syscall_abi.h` are compiled by
  both the kernel and the user runtime (via `-Ikernel/user`), so both sides agree
  on numbers, flags, errno values, and struct layouts.

## Failure modes

| Condition | Result |
| --- | --- |
| Unknown / out-of-range syscall number | `-SYS_ENOSYS` (38) |
| User pointer not mapped / not `PAGE_USER` | `-SYS_EFAULT` (14) |
| Write to a read-only-mapped buffer | `-SYS_EFAULT` (write validation fails) |
| String argument missing a NUL within bound | `user_copy_string_from_user` → `NULL` → `-SYS_EFAULT` |
| `getcwd` buffer too small | `-SYS_ERANGE` (34) |
| Bad fd to read/write/close/lseek/readdir | `-SYS_EBADF` (9) |
| `lseek` to a negative offset | `-SYS_EINVAL` (22) |
| `open` of a missing file without `O_CREAT` | `-SYS_ENOENT` (2) |
| `open` with `O_DIRECTORY` of a non-dir | `-SYS_ENOTDIR` (20) |
| `mkdir`/`unlink` on a read-only fs | `-SYS_EPERM` (1) |
| `chdir` to a non-directory | `-SYS_ENOTDIR` (20) |
| `wait` on a non-child / unknown PID | `-SYS_ECHILD` (10) |
| `spawn` of a missing / malformed program | `-SYS_ENOENT` / `-SYS_ENOEXEC` |
| Ring-3 CPU exception | process FAULTED (exit code `-vector`), kernel survives |
| CPL-3 fault with no current process (impossible) | `kernel_panic` |

## Verification

- **`make verify-syscalls`** — boots the image and expects `[PASS] syscall_test`
  (the user-space `/tests/syscall_test.hxe` exercises the syscall ABI end to end).
- **`make verify-vfs`** — expects `[PASS] vfs_test` and `[PASS] fd_test` (covers the
  fd-based syscalls: open/read/write/close/lseek/readdir).
- **`make verify-process`** — expects `[PASS] spawn_test` (covers `SYS_SPAWN` /
  `SYS_WAIT` / `SYS_EXIT` and exit-code propagation).
- **`make verify-user-mode`** — expects `[OK] Ring 3 entry online` and
  `[USER] hello from ring 3`, proving the trap-and-return path works at all.
- **`make verify-user-fault`** — expects `[OK] User fault isolated` and
  `[OK] Userland foundation tests passed`, proving fault isolation.
- **`make verify-hw-userland`** — expects `[PASS] hwinfo`, `[PASS] drivers`,
  `[PASS] devtree`, `[PASS] lsusb`, `[PASS] hidinfo`, `[PASS] inputtest`,
  exercising the Prompt-6 introspection syscalls (23–28).
- **`make verify-prompt4`** rolls up the syscall/vfs/process/user-mode/fault
  targets.

Boot serial markers from `syscall_init`: `[OK] Syscall vector installed` and
`[OK] Syscall dispatcher online`. The whole table and entry path are wired in
`kernel_main` between `process_system_init()` and `user_init()`.

The numbers, flags, and struct layouts documented here are also consumed by the
user-space runtime (`user/lib/`), which is why `syscall_numbers.h` and
`syscall_abi.h` live under `kernel/user/` and are added to the user include path.

## Future expansion

- **More argument registers in use.** The ABI reserves `r10`, `r8`, `r9` for args
  4–6; current handlers use at most three. New syscalls (e.g. `pread`/`pwrite`,
  `mmap`) can use them without an ABI change.
- **`syscall`/`sysret` fast path.** The header explicitly notes the current design
  uses `int 0x80`; a future MSR-configured `syscall` entry could replace the
  software interrupt for lower latency.
- **Larger read/write transfers.** `sys_read`/`sys_write` currently bound each call
  to a 256-byte kernel chunk; a scatter/gather or per-page copy loop would remove
  the per-call cap.
- **Signals / async notification.** There is no signal delivery mechanism today;
  fault isolation simply terminates the process.
- **A vDSO-style fast path** for `getpid`/`uptime` could avoid the trap entirely.
- **Capability checks.** `mode`/permissions are advisory in v0; argument validation
  could grow access-control checks once a uid/gid model exists.
