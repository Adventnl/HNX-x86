# Coreutils

`user/coreutils/` holds roughly sixty small ring-3 utilities — the userland
command set resolved by the shell as `/bin/<cmd>.hxe`. Each is a single C file
linked against the freestanding user libc (`docs/libc.md`) and the syscall
wrapper layer (`docs/userland.md`). They share a small set of conventions
(argv parsing, exit codes, syscall usage) described at the end of this document.

A handful of closely related introspection tools live in sibling directories
(`user/hw/`, `user/usb/`, `user/input/`) rather than `user/coreutils/`, but they
are built the same way, installed under `/bin`, and used like coreutils; they are
catalogued here in the hardware/USB introspection section and clearly marked with
their directory.

All of these names are in the Makefile `USER_BIN` list and installed into the
initramfs at `/bin/<name>.hxe`.

## Architecture

```
   shell / init  --spawn("/bin/<cmd>.hxe", argv)-->  cmd process
                                                         |
                          int main(int argc, char **argv)
                                 |            |
                    parse argv (manual)   syscalls via user libc wrappers
                                 |            |
                       stdout/stderr     read/write/open/stat/readdir/...
                                 |
                            return code  --> parent's wait_pid()
```

Every program is `main(argc, argv)` (or `main(void)`), does its own flag/argument
parsing by hand (no `getopt`), talks to the kernel through the named wrappers in
`user/include/unistd.h`, and returns an exit status that the spawning process
reads with `wait_pid`. There is no shared "applet" dispatcher: each utility is an
independent executable.

## File map

The catalog below groups the programs by purpose. Paths are relative to `user/`.
File-size and syscall notes reflect the actual sources.

### File operations

| Program | File | One-line description |
| --- | --- | --- |
| `cat` | `coreutils/cat.c` | concatenate files (or stdin) to stdout via read/write |
| `cp` | `coreutils/cp.c` | copy `<src>` to `<dst>` through a read/write loop |
| `mv` | `coreutils/mv.c` | copy `<src>` to `<dst>` then `unlink` src (no rename syscall yet) |
| `rm` | `coreutils/rm.c` | remove files / empty dirs via `unlink` (multiple args) |
| `mkdir` | `coreutils/mkdir.c` | create directories (multiple args) |
| `touch` | `coreutils/touch.c` | create empty files if absent (`open` `O_CREAT`) |
| `ln`-less | — | (no `ln`; the VFS link path is a kernel foundation) |
| `ls` | `coreutils/ls.c` | list a directory (default `.`) via `readdir`; tags dirs `/`, chardevs `@` |
| `readfile` | `coreutils/readfile.c` | print a file's contents to stdout |
| `writefile` | `coreutils/writefile.c` | create/overwrite a file with joined argv text |
| `hexdump` | `coreutils/hexdump.c` | classic offset / hex / ASCII dump of a file |
| `stat` | `coreutils/stat.c` | print a path's type (file/dir/chardev) and size |
| `cmp` | `coreutils/cmp.c` | report first differing byte+line of two files; exit 0 if identical |
| `find` | `coreutils/find.c` | recursively print paths under a dir; `-name` exact basename match |
| `du` | `coreutils/du.c` | recursive byte usage of a file/dir (readdir + stat) |
| `basename` | `coreutils/basename.c` | strip dir components (and optional suffix) from a path |
| `dirname` | `coreutils/dirname.c` | print the directory portion of a path (or `.`) |

### Text processing

| Program | File | One-line description |
| --- | --- | --- |
| `echo` | `coreutils/echo.c` | print argv separated by spaces + newline |
| `head` | `coreutils/head.c` | first N lines (`-n N`, default 10) of files/stdin |
| `tail` | `coreutils/tail.c` | last N lines (`-n N`, default 10) via a fixed line ring |
| `wc` | `coreutils/wc.c` | count lines/words/bytes (`-l`/`-w`/`-c`; all three by default) |
| `grep` | `coreutils/grep.c` | print lines containing a substring; `-n`/`-i`/`-v` |
| `sort` | `coreutils/sort.c` | sort lines lexicographically; `-r` reverse, `-u` drop dup-adjacent |
| `uniq` | `coreutils/uniq.c` | collapse adjacent duplicate lines; `-c` prefix counts |
| `nl` | `coreutils/nl.c` | number nonblank lines of a file/stdin |
| `rev` | `coreutils/rev.c` | reverse the characters of each line |
| `cut` | `coreutils/cut.c` | select fields (`-f`, `-d`) or char positions (`-c`); comma lists, no ranges |
| `tr` | `coreutils/tr.c` | translate set1->set2, or `-d` delete set1 bytes (no ranges/classes) |
| `tee` | `coreutils/tee.c` | copy stdin to stdout and to each named file (up to 8) |
| `seq` | `coreutils/seq.c` | arithmetic sequence: `seq L` / `seq F L` / `seq F I L` |

### System info

| Program | File | One-line description |
| --- | --- | --- |
| `uname` | `coreutils/uname.c` | system identification (`-a` for the long form) |
| `whoami` | `coreutils/whoami.c` | print the single owner, "root" |
| `id` | `coreutils/id.c` | uid/gid/pid/ppid/pgid/sid from the credential syscalls |
| `uptime` | `coreutils/uptime.c` | ms since boot as `up S.mmm s` |
| `date` | `coreutils/date.c` | UTC calendar date from `gettimeofday` + monotonic uptime |
| `meminfo` | `coreutils/meminfo.c` | physical memory stats from `meminfo` (KiB) |
| `free` | `coreutils/free.c` | total/used/free physical memory in KiB |
| `ps` | `coreutils/ps.c` | process table (pid/ppid/state/name) from `ps` |
| `dmesg` | `coreutils/dmesg.c` | kernel hardware/IRQ summary (no message ring exposed) |
| `env` | `coreutils/env.c` | set NAME=VALUE then run a command, or look up a single name |
| `printenv` | `coreutils/printenv.c` | print named environment variables (name required) |
| `ulimit` | `coreutils/ulimit.c` | report fixed per-process resource limits |
| `help` | `coreutils/help.c` | list available shell commands / coreutils |
| `hello` | `coreutils/hello.c` | greet + print pid (target of `spawn_test`) |
| `testread` | `coreutils/testread.c` | read `/dev/zero` and verify it reads as zeroes |
| `keyboard_test` | `coreutils/keyboard_test.c` | report keyboard reachability (decode verified kernel-side) |
| `tty_test` | `coreutils/tty_test.c` | report TTY reachability (cooked input verified kernel-side) |
| `clear` | `coreutils/clear.c` | emit ANSI clear-screen + cursor-home |
| `true` | `coreutils/true.c` | exit 0 |
| `false` | `coreutils/false.c` | exit 1 |
| `yes` | `coreutils/yes.c` | print a string repeatedly (bounded to 8 — no interrupt yet) |
| `sync` | `coreutils/sync.c` | no-op success (VFS is write-through; no dirty buffers) |
| `sleep` | `coreutils/sleep.c` | suspend for `<seconds>` whole seconds |
| `pwd` | `coreutils/pwd.c` | print the working directory |
| `serviced` | `coreutils/serviced.c` | service-manager foundation (see `docs/userland.md`) |

### Process control

| Program | File | One-line description |
| --- | --- | --- |
| `kill` | `coreutils/kill.c` | send a signal number to a pid (`kill [-SIG] <pid>`; delivery is a foundation) |
| `jobs` | `coreutils/jobs.c` | list processes in the caller's session (a job view over `ps`) |
| `time` | `coreutils/time.c` | run a command and report wall-clock ms (`uptime_ms` deltas) |
| `cd`/`pwd`/`exit` | (shell builtins) | run in the shell process — see `docs/shell.md` |

### Storage / filesystem introspection

| Program | File | One-line description |
| --- | --- | --- |
| `df` | `coreutils/df.c` | list mounted filesystems (mount point + type) from `mounts` |
| `mounts` | `coreutils/mounts.c` | list the VFS mount table |
| `devices` | `coreutils/devices.c` | list the driver-core device registry (name + type) |
| `blocks` | `coreutils/blocks.c` | list block devices + geometry (sectors, sector size) |
| `lsblk` | `coreutils/lsblk.c` | block devices with capacity in MiB (alias view of `blocks`) |
| `lspci` | `coreutils/lspci.c` | list PCI functions from the device registry |

### Hardware / USB / input introspection

These live outside `user/coreutils/` (in `user/hw/`, `user/usb/`, `user/input/`)
but are installed under `/bin` and used like coreutils.

| Program | File | One-line description |
| --- | --- | --- |
| `hwinfo` | `hw/hwinfo.c` | one-line hardware inventory from `hw_info` (PCI/devices/IRQ/events) |
| `drivers` | `hw/drivers.c` | devices + bound driver + lifecycle state |
| `devtree` | `hw/devtree.c` | device tree: PCI/driver-core devices, then USB devices |
| `interrupts` | `hw/interrupts.c` | per-vector interrupt counts from `interrupts` |
| `msiinfo` | `hw/msiinfo.c` | per-PCI-function MSI / MSI-X capability summary |
| `powerinfo` | `hw/powerinfo.c` | per-device power state (D0..D3) from the registry |
| `lsusb` | `usb/lsusb.c` | list enumerated USB devices (with speed) |
| `usbinfo` | `usb/usbinfo.c` | detailed per-device USB descriptor summary |
| `hidinfo` | `usb/hidinfo.c` | USB HID devices (keyboard/mouse) + boot protocol |
| `usbtest` | `usb/usbtest.c` | assert ≥1 USB device enumerated with a valid descriptor |
| `inputtest` | `input/inputtest.c` | confirm the unified input pipeline is queryable; drain events |
| `keytest` | `input/keytest.c` | drain pending unified key events; report with source |
| `mousetest` | `input/mousetest.c` | drain pending mouse events; report move/buttons/wheel |

## Data structures

The utilities are mostly stateless filters; the ones that buffer use small fixed
arrays sized for the freestanding environment:

- `sort` (`coreutils/sort.c`): `g_lines[MAXLINES=1024][LINEW=1024]` plus a
  `g_idx[]` pointer array; sorts with libc `qsort` (insertion sort).
- `tail` (`coreutils/tail.c`): a `g_ring[MAXKEEP=256][LINEW=1024]` line ring so it
  never needs the whole file.
- `cut` (`coreutils/cut.c`): `g_sel[MAXSEL=64]` selected field/char numbers.
- `tee` (`coreutils/tee.c`): `outs[MAXOUT=8]` output fds.
- `du`/`find` (`coreutils/du.c`, `find.c`): `PATHMAX=512` path buffers and a
  `join()` helper; both recurse with `readdir` + `stat`.

The introspection tools fill the shared ABI structs (`sys_*` in
`kernel/user/syscall_abi.h`) — `sys_block_entry`, `sys_device_entry`,
`sys_mount_entry`, `sys_meminfo`, `sys_ps_entry`, `sys_hw_info`, `sys_irq_entry`,
`sys_usb_entry`, `sys_msi_entry`, `sys_mouse_event` — by passing a caller-owned
array and a max count to the matching wrapper.

## Key APIs (shared usage)

The utilities draw from a small subset of the user libc and wrappers:

- **I/O:** `open`/`read`/`write`/`close`, `fdgets` (line input that never
  over-reads a pipe/tty), `print`/`eprint`/`printf`/`snprintf`.
- **FS / introspection:** `readdir`, `stat`, `mkdir`, `unlink`, `getcwd`,
  `chdir`, and the structured wrappers `mounts`/`devices`/`blocks`/`meminfo`/
  `ps`/`hw_info`/`interrupts`/`msi_info`/`usb_devices`/`input_poll`/`mouse_poll`.
- **Process / time:** `spawn`+`wait_pid` (`time`), `getpid`/`getppid`/`getuid`/
  `getgid`/`getsid`/`getpgid` (`id`/`jobs`), `kill`, `uptime_ms`,
  `gettimeofday`, `sleep_ms`.
- **Environment:** `env_set`/`env_get` (`env`/`printenv`).
- **Parsing:** `atoi`/`atol`/`strtol`, `strchr`/`strrchr`/`strstr`/`strcmp`,
  `isdigit`/`isspace`/`tolower` from `ctype.h`.

## Representative implementations

A few utilities show the recurring patterns the rest follow.

**The filter pattern (`cat`, `grep`, `wc`, `head`, `nl`, `rev`, `tr`).** A helper
takes an fd and processes it; `main` reads stdin (fd 0) when given no file
argument, otherwise loops over file arguments, `open`ing each. `cat` is the
minimal case:

```c
static int cat_fd(int fd) {
    char buf[256]; long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) write(1, buf, (unsigned long)n);
    return (n < 0) ? 1 : 0;
}
int main(int argc, char **argv) {
    if (argc < 2) return cat_fd(0);
    /* else open each argv[i], cat_fd, accumulate rc */
}
```

Line-oriented filters use `fdgets` instead of raw `read` so a line is read safely
from a pipe/TTY (`grep`, `head`, `tail`, `nl`, `uniq`). Flag parsing is by hand:
`grep` scans leading `-n`/`-i`/`-v` before the pattern; `head`/`tail` read `-n N`
as `atol(argv[i+1])`.

**The recursion pattern (`find`, `du`).** Both define a `join(out, cap, dir,
name)` (slash-aware via `snprintf`) and a recursive walker that `stat`s a path,
and for a directory `open`s it and iterates `readdir`, recursing into
subdirectories. `find` prints matching paths (`-name` exact basename), `du`
accumulates and prints byte subtotals.

**The introspection pattern (`ps`, `blocks`, `devices`, `mounts`, `lspci`,
`free`, `hwinfo`).** Declare a stack array of the relevant `sys_*` struct, call
the wrapper with `(array, max)`, check for a negative return, then print a header
and one formatted row per entry. `blocks` is typical:

```c
struct sys_block_entry b[16];
int n = blocks(b, 16);
if (n < 0) { print("blocks: error\n"); return 1; }
printf("%-10s %12s %s\n", "BLOCKDEV", "SECTORS", "SECSIZE");
for (int i = 0; i < n; i++)
    printf("%-10s %12lu %u\n", b[i].name, (unsigned long)b[i].sectors,
           b[i].sector_size);
```

Several tools are alias/derived views over the same wrapper: `lsblk` vs `blocks`
(`blocks` with MiB formatting), `df` vs `mounts`, `lspci` filters `devices` by
the PCI type. State/enum decoding tables (device/power/speed/input-source names)
are small `static const char*[]` lookups guarded by a range check.

## Conventions

These hold across the catalog:

- **Entry / linkage.** Each program is one `main(argc, argv)` (or `main(void)`)
  C file, linked with `crt0.o` + the six libc objects + its own `.o`, converted
  to `/bin/<name>.hxe` (see `docs/userland.md` for the build chain).
- **Argument parsing is manual.** No `getopt`. Flags are matched positionally
  (e.g. `grep`/`sort`/`uniq`/`cut`/`tr` check `argv[i][0]=='-'` and the specific
  letter); option values like `-n N` use `atol(argv[i+1])`.
- **stdin default.** Filter-style tools (`cat`, `grep`, `head`, `tail`, `wc`,
  `sort`, `uniq`, `nl`, `rev`, `cut`, `tr`, `tee`) read fd 0 when given no file
  argument, and loop over multiple file arguments otherwise.
- **Exit codes.** `0` = success. `1` = a runtime error (open/stat/read failure,
  partial work). `2` = a usage error (wrong argument count); usage text goes to
  **stderr** via `eprint`. `false` returns 1; `true`/`sync` return 0; `cmp`
  returns 0 (same), 1 (differ), 2 (open error).
- **Error messages.** Printed as `<prog>: <detail>` (e.g. `cat: <file>: not
  found`, `cp: <src>: cannot open`); usage as `usage: <prog> ...`.
- **No buffering layer.** Output uses `write`/`print`/`printf` directly; line
  input uses `fdgets`, which reads one byte at a time and is safe on pipes/TTY.
- **Bounded buffers.** Path buffers are typically 256–512 bytes; line buffers
  512–1024; tools truncate rather than overrun (`strncpy`/explicit NUL).
- **Honest about gaps.** Where the kernel lacks a facility, the tool documents it
  in a header comment and degrades gracefully — `mv` copies+unlinks (no rename
  syscall), `sync` is a no-op (write-through VFS), `dmesg` shows the hardware
  summary (no kernel message ring exposed), `kill` records a pending signal
  (delivery is a foundation), `yes` is bounded to 8 iterations (no interrupt),
  `printenv` requires a name (no enumeration API).

## Notable per-utility behaviors

Specifics that are easy to get wrong and are grounded in the sources:

- **`seq`** — argument count selects the form: `seq L` (1..L step 1),
  `seq F L` (F..L step 1), `seq F I L` (F..L step I); values parsed with `atol`.
- **`cut`** — `-f<list>` selects fields (default delimiter TAB, override `-d`);
  `-c<list>` selects 1-based char positions; `<list>` is comma-separated single
  numbers (no ranges), capped at `MAXSEL=64`.
- **`tr`** — `tr set1 set2` translates byte-for-byte by matching index; `tr -d
  set1` deletes; no ranges or character classes.
- **`sort`** — buffers up to `MAXLINES=1024` lines (`LINEW=1024` each), sorts with
  libc `qsort` (insertion sort), `-r` reverses, `-u` drops adjacent dups *after*
  sorting.
- **`tail`** — keeps the last N lines in a `MAXKEEP=256`-slot ring, so it never
  needs the whole file in memory.
- **`grep`** — plain substring match (`strstr`), not regex; `-i` lowercases both
  sides, `-v` inverts, `-n` prefixes line numbers.
- **`wc`** — counts lines (`\n`), words (whitespace-delimited via an `in_word`
  flag), and bytes; `-l`/`-w`/`-c` restrict the output, all three by default.
- **`date`** — converts `gettimeofday` seconds to a UTC calendar date with its own
  leap-year math (`is_leap`) and a month table; also prints uptime.
- **`du`/`find`** — recurse with `readdir`+`stat` and a slash-aware `join`;
  `find -name` is an exact basename compare, not a glob.
- **`ls`** — tags directories with `/` and char devices with `@` based on the
  `sys_dirent.type` field.
- **`ulimit`** — reports fixed compile-time limits (FD_MAX 32, max args 32, stack
  256 KiB, brk 256 KiB, processes 64) rather than querying the kernel.
- **`yes`** — bounded to 8 iterations because the scripted environment has no way
  to interrupt an infinite loop yet.

## Invariants

- **One executable per utility.** No multicall binary; the shell always resolves
  `/bin/<cmd>.hxe`.
- **A utility never persists shell state.** cwd/variable/history changes must be
  shell builtins (`docs/shell.md`); a spawned utility cannot affect its parent
  except through its exit code and its output.
- **Structured syscalls are array+max.** Introspection wrappers take a
  caller-owned array and a capacity and return the count actually written (or a
  negative error); the tools cap their stack arrays (e.g. 8/16/32/64 entries).
- **ABI structs are shared, not duplicated.** Programs include
  `kernel/user/syscall_abi.h` (via `-Ikernel/user`); layouts match the kernel
  byte-for-byte.

## Failure modes

- **Missing/again unreadable file** -> per-file `<prog>: <file>: ...` message,
  exit 1 (filters continue to the next file argument where sensible).
- **Wrong usage** -> usage line on stderr, exit 2.
- **Introspection error** (wrapper returns < 0) -> `<prog>: error`, exit 1.
- **Capacity overflow** in buffering tools (`sort` > 1024 lines, `tail` ring,
  `tee` > 8 files, `cut` > 64 selectors) -> excess is dropped, not overrun.

## Verification

Build target (`Makefile.production`): **`verify-coreutils-expanded`**, expecting:

```
[PASS] coreutils suite
```

emitted by `user/tests/coreutils_test.c` (run by `init` as
`/tests/coreutils_test.hxe`). That test spawns a representative set —
`uname`, `echo hi`, `seq 1 5`, `basename /a/b/c.txt`, `dirname /a/b/c.txt`,
`true` — and checks each exits 0, then creates `/disk/cu_wc.txt` and runs `wc` on
it, printing `[PASS] coreutils suite` only if every one succeeded.

`init` additionally drives the broader coreutils smoke directly (see
`docs/userland.md`): the `mkdir`/`writefile`/`readfile`/`ls`/`stat`/`hexdump`
chain plus `mounts`/`devices`/`blocks`/`lspci`/`lsblk` produce
`[PASS] expanded coreutils`, and the hardware/USB/input tools each produce their
own `[PASS] <tool>` / `[FAIL] <tool>` markers (`hwinfo`, `drivers`, `devtree`,
`lsusb`, `hidinfo`, `inputtest`).

## Future expansion

- **A real `mv`/rename** once the VFS exposes a rename syscall (today `mv` is
  copy+unlink); likewise `ln` once link is exposed to ring 3.
- **`getopt`-style option parsing** to replace the hand-rolled flag checks and
  support combined/long options and ranges (`cut`/`tr` have no ranges; `tr` no
  character classes).
- **Real signal delivery** behind `kill`; today the kernel records a pending
  signal and there are no handlers.
- **Pipelines and redirection** in the shell would let these filters compose
  (the wrappers `pipe`/`dup2` exist; the shell does not wire them yet).
- **`dmesg` over a kernel log ring** and `sync` over real dirty-buffer flushing
  once those kernel facilities exist.
- **Environment enumeration** so `printenv`/`env` can list variables.
