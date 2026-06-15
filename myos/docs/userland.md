# Userland (ring 3)

MyOS runs unprivileged programs in ring 3 as a freestanding, non-POSIX userland.
There is no dynamic linker, no shared libc, and no ELF loader in the kernel:
programs are statically linked against the in-tree user libc, converted to a
minimal custom load format (HXE1), packed into an initramfs, and entered through
a single assembly trampoline (`crt0.S`). Everything the userland can do goes
through the `int 0x80` syscall ABI shared verbatim with the kernel.

This document covers the ring-3 model: the C runtime, the HXE1 executable format
and how it is used, the syscall wrapper layer, the init/process flow, the
`serviced` service-manager foundation, and the userland test matrix. The libc
itself (string/stdio/stdlib/malloc) is documented separately in `docs/libc.md`;
the shell in `docs/shell.md`; the coreutils catalog in `docs/coreutils.md`.

## Architecture

```
   build:  user/*.c  --clang-->  *.o  --ld.lld + linker.ld-->  *.elf
                                    |
                          tools/mkhxe.py (ELF64 -> HXE1)
                                    |
                          *.hxe  --tools/mkinitramfs.py-->  initramfs.hxf
                                    |
   boot:   kernel mounts initramfs -> spawns /bin/init.hxe (PID 1)

   run:    init spawn()s test programs + shells, wait_pid()s each
                                    |
   per program:  kernel exec_load() maps HXE1 segments + stack + heap
                                    |
                _start (crt0.S):  argc/argv -> main() -> SYS_EXIT
                                    |
   any libc call -> __syscall(int 0x80) -> kernel dispatcher -> rax result
```

A program's address space (`kernel/user/user.h`):

| Region | Base | Notes |
| --- | --- | --- |
| HXE1 image | `USER_IMAGE_BASE = 0x400000` | code RX, data/bss RW (per segment flags) |
| User heap | `USER_HEAP_BASE = 0x4000000000` | bump heap; 1 MiB (`USER_HEAP_INITIAL`) pre-mapped on spawn, 16 MiB reserved |
| User stack | `USER_STACK_TOP = 0x7FFFFFFFE000` | 256 KiB (`USER_STACK_SIZE`); argv block lives here |
| `USER_TOP` | `0x800000000000` | all user virtual addresses below this |

Ring-3 selectors are `USER_CODE_SELECTOR_RPL3 = 0x23` and
`USER_DATA_SELECTOR_RPL3 = 0x1B`. The kernel enters ring 3 via
`user_enter_ring3()` (an `iretq` frame with IF set); each user CR3 mirrors the
kernel's low footprint, framebuffer, LAPIC MMIO and the (supervisor) initramfs so
syscalls/IRQs/faults can run and file data can be read while a user CR3 is active.

## File map

| File | Role |
| --- | --- |
| `user/lib/crt0.S` | `_start`: unpack argc/argv from the stack, call `main`, `SYS_EXIT` |
| `user/lib/syscall.c` | `__syscall(number,a0,a1,a2)` — the raw `int 0x80` wrapper |
| `user/lib/unistd.c` | Named syscall wrappers (write/read/open/spawn/wait/...) |
| `user/lib/stdio.c` | print helpers + `printf`/`vformat`/`snprintf`/`fdgets` |
| `user/lib/stdlib.c` | `exit`, numeric parsing, `qsort`/`bsearch`, `strerror` |
| `user/lib/string.c` | freestanding `mem*`/`str*` (clang may emit `memcpy`/`memset`) |
| `user/lib/malloc.c` | bump heap allocator over the kernel-mapped user heap |
| `user/include/*.h` | the user libc headers (see `docs/libc.md`) |
| `kernel/user/syscall_numbers.h` | shared ABI: `SYS_*` numbers, errno values, flags |
| `kernel/user/syscall_abi.h` | shared ABI: C structs for structured syscalls |
| `user/include/syscall.h` / `unistd.h` | user-side syscall + wrapper decls |
| `user/init/init.c` | PID 1: runs the test matrix, then the shells |
| `user/coreutils/serviced.c` | service-manager foundation (init/devd/logd/netd/mountd) |
| `user/shell/*.c` | the shell (see `docs/shell.md`) |
| `user/coreutils/*.c` | ~60 utilities (see `docs/coreutils.md`) |
| `user/tests/*.c` | ring-3 test programs (syscall/fd/vfs/spawn/libc/coreutils/...) |
| `tools/mkhxe.py` | ELF64 -> HXE1 converter |
| `tools/inspect_hxe.py` | dump an HXE1 header + segment table |
| `tools/mkinitramfs.py` | pack `(archive_path, host_file)` pairs into `initramfs.hxf` |
| `kernel/process/exec.h` / `exec.c` | kernel-side HXE1 loader (`exec_load`) |

## Data structures

**The C runtime entry (`crt0.S`).** The kernel enters at `_start` with a
SysV-style stack: `[rsp+0] = argc`, `[rsp+8..] = argv[0..argc-1], NULL, strings`,
RSP 16-byte aligned. `_start` zeroes `rbp` (terminates the frame chain), loads
`argc` into `rdi` and `&argv[0]` into `rsi`, calls `main`, then moves `main`'s
return value into `edi` and issues `SYS_EXIT` via `int 0x80` (never returns; a
`jmp` self-loop guards against return). It includes only `syscall_numbers.h` (no
C ABI structs) and emits a `.note.GNU-stack` section.

**The syscall ABI (`kernel/user/syscall_numbers.h`).** `SYSCALL_VECTOR = 0x80`.
Number in `rax`; args in `rdi`/`rsi`/`rdx` (the user wrapper uses three; the ABI
reserves `r10`/`r8`/`r9` for future expansion). Result in `rax`, with negative
values being `-errno`. Numbers run `SYS_EXIT=0 .. SYS_ENV_SET=56`, with
`SYS_MAX_NR=57` one past the highest. Constant groups defined here include
`waitpid` options (`WNOHANG`/`WUNTRACED`), `fcntl` commands (`F_DUPFD`/`F_GETFD`
/...), `mmap` prot/flags, clock ids, `lseek` whence, `open` flags
(`O_RDONLY`/`O_WRONLY`/`O_RDWR`/`O_CREAT`/`O_TRUNC`/`O_DIRECTORY`), and the errno
numbers (`SYS_EPERM=1` .. `SYS_ENOSYS=38`, plus `SYS_ENOEXEC=8`).

**Structured-syscall structs (`kernel/user/syscall_abi.h`).** Fixed-width,
byte-for-byte agreed between kernel and userland: `sys_dirent`, `sys_stat`,
`sys_meminfo`, `sys_ps_entry`, `sys_mount_entry`, `sys_device_entry`,
`sys_usb_entry`, `sys_hw_info`, `sys_irq_entry`, `sys_input_event`,
`sys_mouse_event`, `sys_msi_entry`, `sys_block_entry`, `sys_timeval`,
`sys_timespec`. Never included by assembly.

**The HXE1 executable format (`tools/mkhxe.py`).** HXE1 ("HXE1" =
`0x31455848`) is a minimal load format so the kernel never parses ELF in ring 0:

```c
struct hxe_header  { u32 magic; u32 version; u64 entry;
                     u64 segment_count; u64 header_size; }      /* 32 bytes */
struct hxe_segment { u64 virtual_address; u64 memory_size;
                     u64 file_size; u64 file_offset; u64 flags; } /* 40 bytes x N */
/* then: segment file bytes (file_size each, in segment order) */
```

Segment flags are `HXE_SEG_READ=1`, `HXE_SEG_WRITE=2`, `HXE_SEG_EXEC=4`. `mkhxe.py`
reads a linked **ELF64 ET_EXEC** (non-PIE, x86-64, little-endian), keeps only
`PT_LOAD` segments, maps `PF_R/W/X` to the HXE flags, and emits the header +
segment table + blobs.

## Key APIs

**The raw wrapper (`user/lib/syscall.c`):**

```c
long __syscall(long number, long a0, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(number), "D"(a0), "S"(a1), "d"(a2)
                     : "memory");
    return ret;
}
```

The kernel preserves every GPR except `rax`, so `rax` is the sole output; the
`"memory"` clobber stops the compiler caching across the trap.

**Named wrappers (`user/lib/unistd.c`, decls in `user/include/unistd.h`).** Each
is a thin call into `__syscall`. Coverage by area:

- **I/O & FS:** `write`, `read`, `open`, `close`, `lseek`, `readdir`, `mkdir`,
  `unlink`, `stat`.
- **Process:** `getpid`, `getppid`, `gettid`, `yield`, `sleep_ms`, `spawn`,
  `wait_pid`, `waitpid`, `kill`, `getpgid`/`setpgid`, `getsid`/`setsid`.
- **Credentials:** `getuid`/`setuid`, `getgid`/`setgid`,
  `getpriority`/`setpriority`.
- **Memory:** `brk`, `sbrk`, `mmap`, `munmap` (`sbrk`/`mmap` return `(void*)-1`
  on a negative result).
- **FDs:** `dup`, `dup2`, `fcntl`, `ioctl`, `pipe`.
- **Time:** `uptime_ms`, `gettimeofday`, `clock_gettime`, `nanosleep`.
- **Introspection:** `meminfo`, `ps`, `mounts`, `devices`, `blocks`,
  `usb_devices`, `hw_info`, `interrupts`, `input_poll`, `mouse_poll`, `msi_info`.
- **Environment:** `env_set`, `env_get`.
- **CWD:** `getcwd`, `chdir`.

**Kernel-side load (`kernel/process/exec.h`).** `exec_load(cwd, path, space,
out_entry)` resolves an HXE1 path through the VFS and maps its segments into a
user address space, writing the entry point. Must run with the kernel CR3 active.

## The syscall number space

`SYS_*` numbers run 0..56 (`SYS_MAX_NR=57`). They were added in waves; the
grouping below mirrors the comments in `syscall_numbers.h`:

| Range | Group | Members |
| --- | --- | --- |
| 0–16 | core | `EXIT`, `WRITE`, `READ`, `SLEEP`, `GETPID`, `YIELD`, `OPEN`, `CLOSE`, `LSEEK`, `READDIR`, `SPAWN`, `WAIT`, `GETCWD`, `CHDIR`, `UPTIME`, `MEMINFO`, `PS` |
| 17–22 | namespace + storage | `MKDIR`, `UNLINK`, `STAT`, `MOUNT_INFO`, `DEVICES`, `BLOCKS` |
| 23–28 | HW / USB / input (Prompt 6) | `USB_DEVICES`, `HW_INFO`, `INTERRUPTS`, `INPUT_POLL`, `MOUSE_POLL`, `MSI_INFO` |
| 29–56 | process / cred / time / fd / mem / env (Work Unit B) | `GETPPID`, `GETTID`, `GETUID`/`SETUID`, `GETGID`/`SETGID`, `GETPRIORITY`/`SETPRIORITY`, `BRK`, `SBRK`, `MMAP`, `MUNMAP`, `DUP`, `DUP2`, `PIPE`, `FCNTL`, `IOCTL`, `WAITPID`, `KILL`, `GETTIMEOFDAY`, `CLOCK_GETTIME`, `NANOSLEEP`, `GETPGID`/`SETPGID`, `GETSID`/`SETSID`, `ENV_GET`, `ENV_SET` |

The errno set (returned negated) is a POSIX-numbered subset: `SYS_EPERM=1`,
`SYS_ENOENT=2`, `SYS_ESRCH=3`, `SYS_EIO=5`, `SYS_ENOEXEC=8`, `SYS_EBADF=9`,
`SYS_ECHILD=10`, `SYS_ENOMEM=12`, `SYS_EFAULT=14`, `SYS_EEXIST=17`,
`SYS_ENOTDIR=20`, `SYS_EISDIR=21`, `SYS_EINVAL=22`, `SYS_EMFILE=24`,
`SYS_ERANGE=34`, `SYS_ENAMETOOLONG=36`, `SYS_ENOSYS=38`. MyOS does not claim
POSIX compatibility; the names just ease future expansion.

## Process lifecycle

A program's life from `spawn` to reap:

1. **spawn.** The parent calls `spawn(path, argv)` -> `SYS_SPAWN`. The kernel
   creates an address space, calls `exec_load` to map the HXE1 image at
   `USER_IMAGE_BASE`, maps a 256 KiB stack at `USER_STACK_TOP`, pre-maps 1 MiB of
   heap at `USER_HEAP_BASE`, lays out argc/argv on the stack SysV-style, and
   returns the child pid (or a negative error).
2. **entry.** The kernel `iretq`s into ring 3 at `_start` with the user CR3
   loaded; `crt0.S` unpacks argc/argv and calls `main`.
3. **run.** The program executes, trapping into the kernel for each libc call via
   `int 0x80`. IRQs and faults are serviced with the user CR3 active because each
   address space mirrors the kernel's low footprint.
4. **exit.** `main` returns (or the program calls `exit`); `crt0`/`exit` issues
   `SYS_EXIT` with the code. The process enters the EXIT (zombie) state.
5. **reap.** The parent's `wait_pid(pid, &code)` (`SYS_WAIT`) collects the exit
   code and lets the kernel free the zombie. `init` and the shell both reap every
   child they spawn.

## Build chain

The Makefile builds the userland in four stages (flags from the `Makefile`
`USER_*` variables):

1. **Compile** each `.c` to a `.o` with
   `USER_CFLAGS = --target=x86_64-unknown-none-elf -ffreestanding
   -fno-stack-protector -fno-builtin -fno-pic -fno-pie -mno-red-zone -mno-sse
   -mno-mmx -msoft-float -nostdlib -Wall -Wextra -Iuser/include -Ikernel/user`.
   `crt0.S` is assembled with `USER_ASFLAGS`. SSE/MMX are disabled because IRQ
   stubs save only GPRs (an async IRQ must never corrupt FP/SIMD state).
2. **Link** `crt0.o + <name>.o + USER_LIB_OBJ` into a `.elf` with
   `USER_LDFLAGS = -T user/linker.ld -z max-page-size=0x1000`. The 4 KiB page max
   overrides ld.lld's 2 MiB x86-64 default so segments sit page-tight at
   `USER_IMAGE_BASE`. `USER_LIB_OBJ` is `syscall.o string.o stdlib.o malloc.o
   stdio.o unistd.o`. The shell links its extra objects (`parser.o`/`builtins.o`)
   alongside `shell.o`.
3. **Convert** the ELF to HXE1 with `tools/mkhxe.py <elf> <out.hxe>`.
4. **Pack** all `/bin/<p>.hxe` and `/tests/<p>.hxe` into `initramfs.hxf` via
   `tools/mkinitramfs.py`, which takes `(archive_path, host_file)` pairs.

`USER_BIN` and `USER_TEST` in the Makefile are the authoritative program lists.
`tools/inspect_hxe.py` dumps an HXE1 header + segment table for debugging.

## Worked example: a syscall round trip

`printf("hi\n")` from a coreutil:

1. `printf` formats into its 256-byte flush buffer (see `docs/libc.md`) and calls
   `write(1, buf, 3)` (`user/lib/stdio.c`).
2. `write` (`user/lib/unistd.c`) calls
   `__syscall(SYS_WRITE, 1, (long)buf, 3)`.
3. `__syscall` (`user/lib/syscall.c`) loads `rax=SYS_WRITE(1)`, `rdi=1`,
   `rsi=buf`, `rdx=3`, and executes `int $0x80`.
4. The kernel's vector-`0x80` handler dispatches by `rax`, performs the write to
   the console device, and returns the byte count in `rax`.
5. The wrapper returns that count as a `long`; a negative value would be
   `-errno`. `printf` ignores it (best-effort output).

The shared `syscall_numbers.h`/`syscall_abi.h` (included via `-Ikernel/user`)
guarantee the number and any struct layout match the kernel exactly.

## Ring-3 test programs

`user/tests/` holds the programs `init` spawns as `/tests/<name>.hxe`. They print
their own `[PASS]`/`[FAIL]` markers (most via `utest.h`) and exit 0/1:

| Program | Exercises |
| --- | --- |
| `syscall_test` | basic syscall surface |
| `fd_test` / `fd_stress_test` | file-descriptor open/close/dup behavior + stress |
| `vfs_test` | VFS path/dir operations |
| `spawn_test` | spawn/wait of a child (`hello.hxe`) |
| `fault_test` | deliberately faults in ring 3 (kernel isolates it) |
| `process_tree_test` | process parent/child relationships |
| `syscall_stress_test` | syscall throughput/robustness |
| `memory_map_test` | brk/mmap user-memory mapping |
| `libc_test` | the freestanding libc (`docs/libc.md`) |
| `coreutils_test` | spawns a representative coreutils set |
| `storage_test`/`fs_test`/`disk_test`/`cache_test` | storage stack (`user/storage/`) |
| `usb_test`/`hid_test`/`input_test`/`msi_test` | HW/USB/input assertions |

## Invariants

- **One ABI source of truth.** The user runtime compiles with `-Ikernel/user` so
  it includes the kernel's `syscall_numbers.h` and `syscall_abi.h` directly; the
  two sides cannot drift.
- **`crt0` is the only entry.** Every program links `crt0.o` + its `<name>.o` +
  the six libc objects (`syscall/string/stdlib/malloc/stdio/unistd.o`). `main`'s
  `int` return becomes the process exit code.
- **Negative result = -errno.** Wrappers return the raw `long`; callers compare
  `< 0`. There is no global `errno` variable; `strerror()` negates and renders.
- **Freestanding build.** `USER_CFLAGS` is `-ffreestanding -fno-builtin -fno-pic
  -fno-pie -mno-red-zone -mno-sse -mno-mmx -msoft-float -nostdlib`; programs link
  with `-T user/linker.ld -z max-page-size=0x1000` (4 KiB granularity, not the
  x86-64 default 2 MiB) so HXE segments are page-tight at `USER_IMAGE_BASE`.
- **HXE validation is enforced at build time.** `mkhxe.py` rejects non-ET_EXEC /
  PIE / non-x86-64 images, `file_size > memory_size`, segments below
  `USER_IMAGE_BASE` or crossing `USER_TOP`, overlapping (page-granular) segments,
  and an entry point that is not inside an executable segment.
- **`spawn` + `wait_pid` is the process model.** A parent spawns a child by HXE
  path and an argv vector, then blocks in `wait_pid` for the exit code. Programs
  resolved by the shell live at `/bin/<cmd>.hxe`; tests at `/tests/<name>.hxe`.
- **Single owner.** `whoami` is "root"; `id` reports uid/gid from the credential
  syscalls. MyOS has one user in v0.

## Failure modes

- **`spawn` failure** returns a negative pid; `init`/the shell print a
  "spawn failed"/"command not found" message and propagate the error.
- **A faulting program is isolated.** `init` deliberately runs
  `/tests/fault_test.hxe`, which faults in ring 3; the kernel isolates it (the
  process enters the FAULT state) and the supervisor continues. The normal boot
  expects this fault.
- **Out-of-heap `malloc`** returns NULL (the bump allocator cannot grow past the
  mapped 1 MiB window — see `docs/libc.md`).
- **Unimplemented / invalid syscall** returns `-SYS_ENOSYS`.
- **Bad HXE at load** is rejected by `exec_load` (`-SYS_ENOEXEC` class).

## Verification

Userland verify targets (in `Makefile.production`, run under QEMU via
`tools/verify_qemu.py`):

| Target | Marker(s) expected |
| --- | --- |
| `verify-libc-expanded` | `[PASS] libc runtime tests` |
| `verify-shell-expanded` | `[PASS] shell expanded tests` |
| `verify-coreutils-expanded` | `[PASS] coreutils suite` |
| `verify-services` | `[PASS] service manager foundation`, `[OK] Userland production foundation online` |

**The init test matrix (`user/init/init.c`).** PID 1 prints
`[USER] hello from ring 3`, cats `/etc/banner.txt`, then runs (each spawned and
waited):

1. **Prompt-4 matrix:** `syscall_test`, `fd_test`, `vfs_test`, `spawn_test`,
   `fault_test` (faults; isolated by the kernel).
2. **Work-Unit-B expansion:** `process_tree_test`, `fd_stress_test`,
   `syscall_stress_test`, `memory_map_test`, then prints
   `[OK] Process/syscall production foundation online`.
3. **Work-Unit-G userland:** `libc_test`, `coreutils_test`, `serviced.hxe`, then
   `[OK] Userland production foundation online`.
4. **Expanded coreutils smoke (Prompt 5):** `mkdir`/`writefile`/`readfile`/`ls`/
   `stat`/`hexdump` against `/disk`, plus `mounts`/`devices`/`blocks`/`lspci`/
   `lsblk`; prints `[PASS] expanded coreutils` on success.
5. **Storage programs:** `storage_test`, `fs_test`, `disk_test`, `cache_test`;
   prints `[PASS] storage user programs`.
6. **Prompt-6 HW/USB/input tools:** `hwinfo`, `drivers`, `devtree`, `lsusb`,
   `hidinfo`, `inputtest` (each `[PASS]`/`[FAIL]`), plus informational tools
   (`interrupts`, `msiinfo`, `powerinfo`, `usbinfo`, `keytest`, `mousetest`,
   `usbtest`) and ring-3 assertion tests (`usb_test`, `hid_test`, `input_test`,
   `msi_test`).
7. **Shells:** the scripted shell then the interactive shell (`shell -i`).
8. Cats `/etc/motd.txt` and prints `[init] storage + device expansion complete`.

**`serviced` (`user/coreutils/serviced.c`).** A static registry of five service
foundations — `init` (registry, `getpid()>0`), `devd` (hardware events, validated
via `devices()`), `logd` (`write(1,"",0)`), `netd` (`hw_info()`), `mountd`
(`mounts()`). Each `start()` validates its prerequisite via an existing syscall;
the manager prints a per-service status table, a "N/M services running" summary,
and `[PASS] service manager foundation` only when all five start
(`enum svc_state { SVC_STOPPED, SVC_RUNNING, SVC_FAILED }`).

**libc/coreutils test programs.** `user/tests/libc_test.c` exercises
string/stdlib/ctype/stdio/malloc and prints `[PASS] libc runtime tests` via the
`utest.h` harness (see `docs/libc.md`). `user/tests/coreutils_test.c` spawns a
representative set (`uname`/`echo`/`seq`/`basename`/`dirname`/`true`/`wc`),
checks each exits 0, and prints `[PASS] coreutils suite`.

## Future expansion

- **Per-process FS/socket descriptors** beyond the current fixed FD set; the
  socket layer (see `docs/networking.md`) is kernel-global today.
- **Real `free`/`mmap`-backed heap.** The user `malloc` never reclaims; `sbrk`/
  `mmap` wrappers exist but `malloc.c` does not use them yet (it bump-allocates
  the pre-mapped 1 MiB window).
- **Signals.** `kill` records a pending signal (foundation); no handlers/delivery.
- **Pipes & job control.** `pipe`/`dup2`/`setpgid`/`setsid` wrappers exist; the
  shell does not yet wire pipelines or background jobs (`jobs` reads `ps`).
- **A real `serviced`** with long-running daemons plugging into the foundation.
- **`env` enumeration.** `printenv` requires a name because there is no
  environment-listing syscall.
- **POSIX-ish growth.** The wrapper names are deliberately POSIX-shaped to ease
  future expansion, but MyOS does not claim POSIX compatibility.
