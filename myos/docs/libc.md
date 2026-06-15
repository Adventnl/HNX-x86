# Freestanding user libc

`user/lib/` and `user/include/` implement a small freestanding C library for
ring-3 programs. There is no host libc, no `FILE`, no locale, no wide chars, and
no global `errno`. Every I/O path bottoms out in the `write`/`read` syscalls
(see `docs/userland.md` for the syscall layer). The library is split across six
linked objects plus several header-only modules:

| Object (`user/lib/`) | Provides |
| --- | --- |
| `string.c` | `mem*` and `str*` |
| `stdlib.c` | `exit`, numeric parsing, `qsort`/`bsearch`, `strerror` |
| `malloc.c` | `malloc`/`free`/`calloc`/`realloc` (bump allocator) |
| `stdio.c` | print helpers, `printf`/`vsnprintf`/`snprintf`, `fdgets` |
| `unistd.c` | named syscall wrappers (documented in `docs/userland.md`) |
| `syscall.c` | the raw `int 0x80` trampoline |

Header-only modules (no object to link): `ctype.h`, `errno.h`, `assert.h`,
`utest.h`, `types.h`.

## Architecture

```
   printf("%d", x)            snprintf(buf, n, "%d", x)
        |                              |
   struct out{ fd=1 }            struct out{ dst=buf, cap=n }
         \                           /
          ----- vformat(&o, fmt, ap) -----     (one formatter, two sinks)
                       |
                  oputc / oflush
                       |
              fd>=0: write(fd, ...)     dst: copy into bounded buffer
```

The design principle throughout is "smallest correct implementation that the
userland actually needs": `qsort` is insertion sort (stable, no recursion, fine
for small arrays); `malloc` is a bump allocator with a size header so `realloc`
can copy; `printf` is a single formatter feeding either an fd or a buffer; the
string and ctype routines are textbook. `string.c` exists in part because clang
may emit `memcpy`/`memset` calls for aggregate initialization even when the
program does not call them directly.

## File map

| File | Role |
| --- | --- |
| `user/lib/string.c` | `memset/memcpy/memmove/memcmp/memchr`, `strlen/strcmp/strncmp`, `strcpy/strncpy/strcat/strncat`, `strchr/strrchr/strstr`, `strdup`, `strspn/strcspn`, `strtok_r` |
| `user/include/string.h` | declarations for the above |
| `user/lib/stdlib.c` | `exit`, `atoi/atol/strtol`, `abs/labs`, `qsort/bsearch`, `strerror` |
| `user/include/stdlib.h` | `stdlib` decls + inline `imin/imax/lmin/lmax` |
| `user/lib/malloc.c` | `malloc/realloc/calloc/free` over the user heap |
| `user/lib/stdio.c` | `print/eprint/putchar/puts/fputs`, `print_u64/print_i64`, `printf/vsnprintf/snprintf`, `fdgets` |
| `user/include/stdio.h` | `stdio` decls |
| `user/include/ctype.h` | inline ASCII classification (`isdigit`...`tolower`) |
| `user/include/errno.h` | `E*` names mapped to `SYS_E*`; `strerror` decl |
| `user/include/assert.h` | `assert(expr)` macro (compiled out by `NDEBUG`) |
| `user/include/utest.h` | `UT_BEGIN/UT_CHECK/UT_CHECK_EQ/UT_RESULT/UT_FAILED` test harness |
| `user/include/types.h` | fixed-width types, `NULL`, `bool`/`true`/`false` |
| `user/tests/libc_test.c` | the libc runtime test (emits `[PASS] libc runtime tests`) |

## Data structures

**Fixed-width types (`types.h`).** `uint8_t`...`uint64_t`, signed counterparts,
`size_t`/`ssize_t`/`uintptr_t` all defined on top of the C built-in types (no
`stdint.h`). `size_t` is `unsigned long long`. `NULL`, `bool`, `true`, `false`
are provided here.

**The printf sink (`stdio.c`):**

```c
#define PBUF 256
struct out {
    char  buf[PBUF];   /* flush buffer when writing to an fd */
    int   len;         /* bytes currently in buf */
    int   total;       /* chars that *would* be written (snprintf semantics) */
    int   fd;          /* >=0: write to this fd; <0: write to dst */
    char *dst;         /* snprintf destination, or NULL */
    size_t cap;        /* dst capacity including the NUL */
};
```

`printf` constructs `struct out{ fd=1 }`; `vsnprintf` constructs
`struct out{ fd=-1, dst=buf, cap=size }`. `oputc` either appends to the 256-byte
flush buffer (flushing with `write` when full) or copies into the bounded
destination while `total + 1 < cap`. `total` always counts what *would* have been
written, giving correct `snprintf` return values even on truncation.

**The malloc heap (`malloc.c`).** No struct — the heap is a single bump cursor:

```c
#define USER_HEAP_BASE   0x0000004000000000ULL   /* matches kernel user.h */
#define USER_HEAP_MAPPED 0x0000000000100000ULL   /* matches USER_HEAP_INITIAL (1 MiB) */
#define ALIGN            16ULL
static uint64_t g_cursor = USER_HEAP_BASE;
```

Each allocation is laid out as an `ALIGN`-byte (16) header containing the payload
size, immediately followed by the payload. The header lets `realloc` know how
many bytes to copy.

```
   g_cursor ─┐
             v
   ... | [16-byte size header] | payload (size bytes) | ...
              ^ hdr = align_up(g_cursor,16)            ^ new g_cursor = start+size
                                ^ start = hdr + 16  (returned to the caller)
```

`malloc(size)`:
- `size == 0` -> NULL.
- compute `hdr = align_up(g_cursor, 16)`, `start = hdr + 16`, `end = start + size`.
- if `end > USER_HEAP_BASE + USER_HEAP_MAPPED` -> NULL (out of mapped heap).
- store `size` at `hdr`, set `g_cursor = end`, return `start`.

`realloc(ptr, size)`:
- `ptr == NULL` -> `malloc(size)`; `size == 0` -> `free(ptr)` + NULL.
- read the old size from `*(uint64_t*)(ptr - 16)`, `malloc(size)`, copy
  `min(old, size)` bytes, return the new block (the old block leaks — `free`
  reclaims nothing).

`calloc(count, size)` is `malloc(count*size)` + `memset 0` (no overflow check on
`count*size` — a documented gap). `free(ptr)` is a no-op.

## The printf formatter, step by step

`vformat(struct out *o, const char *fmt, va_list ap)` is the single engine. For
each character:

1. A non-`%` char is emitted directly with `oputc`.
2. After `%`, flags are parsed in a loop: `-` sets left-justify, `0` sets
   zero-pad. Then a decimal field width is read. Then one or more `l` length
   modifiers are consumed (`lng` counts them). The pad char is `'0'` only when
   zero-padding and not left-justifying, else `' '`.
3. The conversion char selects a branch:
   - `%s` — `const char*` (NULL rendered as `"(null)"`), emitted in the field.
   - `%c` — one char from an `int` arg.
   - `%d`/`%i` — signed; `lng` selects `long` vs `int`; negatives get a `'-'`
     then the magnitude via `u64_to_str`.
   - `%u` — unsigned decimal; `lng` selects `unsigned long`.
   - `%x`/`%X` — hex (`%X` uppercase).
   - `%p` — pointer, prefixed `0x`, hex, space-padded.
   - `%%` — literal percent. A trailing `%` at end-of-string ends formatting.
   - anything else — echoes `%` then the char.
4. `emit_field` does the width/justify/pad work; `u64_to_str` does the radix
   conversion (digits reversed into a temp, then copied out).

`oflush` writes the fd flush buffer at the end (fd sink only). `printf` returns
the character count; `snprintf` returns the would-have-written count and bounds
the copy.

## Function reference

**`string.c`** behaviors worth knowing:

| Function | Behavior |
| --- | --- |
| `memmove` | copies backward when `dst > src`; no-op if `dst==src` or `n==0` |
| `strchr`/`strrchr` | `c==0` matches the terminator (returns the NUL position) |
| `strncpy` | NUL-pads the remainder of `n` (POSIX semantics) |
| `strstr` | naive O(n·m) substring; empty needle returns the haystack |
| `strdup` | `malloc`-backed copy (NULL if the heap is exhausted) |
| `strspn`/`strcspn` | accept/reject span lengths, built on `strchr` |
| `strtok_r` | reentrant tokenizer using an explicit `saveptr` |

**`stdlib.c`** behaviors:

| Function | Behavior |
| --- | --- |
| `atoi` | base-10 only; skips spaces/tabs; one optional sign |
| `strtol` | whitespace + sign + `0x`/`0` prefix detection; `endptr` set to first unconsumed (or `start` if none) |
| `qsort` | insertion sort; stack scratch of 256 bytes; no-op for `size==0` or `size>256` |
| `bsearch` | classic binary search; NULL on miss |
| `strerror` | negates negatives; maps `SYS_E*` to short strings; default "Unknown error" |

## Key APIs

**string.h** — the standard `mem*`/`str*` set. Notable behaviors:
`memmove` handles overlap by copying backward when `dst > src`; `strchr`/
`strrchr` treat `c == 0` as matching the terminator (return the NUL position);
`strdup` is `malloc`-backed; `strtok_r` is the reentrant tokenizer used by
programs that need it (the shell uses its own tokenizer — see `docs/shell.md`).

**stdlib.h**
- `void exit(int code)` — `SYS_EXIT`; never returns (infinite loop guard).
- `int atoi(const char*)` — base-10, leading-space skip, optional sign.
- `long atol(const char*)` — `strtol(s, NULL, 10)`.
- `long strtol(const char*, char**endptr, int base)` — skips whitespace, sign,
  `0x`/`0X` prefix (base 0 or 16), leading `0` (base 0 -> octal); sets `*endptr`
  to the first unconsumed char (or `start` if nothing parsed).
- `int abs(int)`, `long labs(long)`.
- `void qsort(base, nmemb, size, cmp)` — **insertion sort**; stable, no
  recursion; uses a 256-byte stack scratch element and is a no-op if
  `size == 0 || size > 256`.
- `void *bsearch(key, base, nmemb, size, cmp)` — binary search; returns NULL on
  miss.
- `const char *strerror(int err)` — negates a negative `err`, maps the `SYS_E*`
  numbers to short strings ("No such file or directory", etc.), default
  "Unknown error".
- inline `imin/imax/lmin/lmax` (in `stdlib.h`).

**stdio.h**
- `long print(const char*)` / `long eprint(const char*)` — `write(1|2, s,
  strlen(s))`.
- `int putchar(int)`, `int puts(const char*)` (adds `\n`), `int fputs(const
  char*, int fd)`.
- `void print_u64(uint64_t)` / `void print_i64(int64_t)` — direct decimal.
- `int printf(const char *fmt, ...)`.
- `int vsnprintf(char*, size_t, const char*, va_list)` /
  `int snprintf(char*, size_t, const char*, ...)`.
- `long fdgets(int fd, char *buf, size_t size)` — reads one line **one byte at a
  time** (so it never over-reads a pipe/tty), stops at `\n` or EOF, always
  NUL-terminates when `size > 0`; returns bytes stored (0 = EOF, <0 = error).

**printf format support (`vformat`).** Conversions: `%s` (NULL -> "(null)"),
`%c`, `%d`/`%i`, `%u`, `%x`/`%X`, `%p` (prefixes `0x`), `%%`. Flags: `-`
(left-justify) and `0` (zero-pad, ignored when left-justifying). A numeric field
width is parsed. The `l` length modifier (one or more `l`s) selects `long`/
`unsigned long`. Unknown conversions echo `%` plus the char. There is no
precision, no `%f` (the kernel/userland are soft-float and avoid SIMD).

**ctype.h** — all inline: `isdigit`, `isupper`, `islower`, `isalpha`, `isalnum`,
`isspace`, `isxdigit`, `isprint`, `iscntrl`, `ispunct`, `toupper`, `tolower`.
ASCII only.

**errno.h** — `EPERM`...`ENOEXEC` aliases for the shared `SYS_E*` numbers, plus
the `strerror` declaration. There is **no** global `errno` object: syscalls
return `-errno` directly and callers compare the negated result symbolically.

**assert.h** — `assert(expr)` prints `assertion failed: <expr> (<file>:<line>)`
and `exit(1)` on failure; compiled to `((void)0)` when `NDEBUG` is defined.

**utest.h** — a tiny test harness mirroring the kernel `ktest.h`. `UT_BEGIN()`
declares a fail flag; `UT_CHECK(cond, msg)` and `UT_CHECK_EQ(a, b, msg)` print
`[CHECK FAILED] ...` and set the flag; `UT_RESULT(marker)` prints `[PASS] marker`
or `[FAIL] marker`; `UT_FAILED()` reads the flag. The verify targets grep for the
`[PASS]` lines, so a failed check simply makes the marker absent.

## Header-only modules in detail

These compile to no object; they are pure inline / macro headers so a program can
use them without adding a link dependency.

**`ctype.h`.** Twelve ASCII classifiers/converters, each a one-line
`static inline`. `isalpha = isupper||islower`, `isalnum = isalpha||isdigit`,
`ispunct = isprint && !space && !alnum`, `iscntrl` covers `0x00–0x1F` and `0x7F`,
`isprint` is `0x20–0x7E`. `toupper`/`tolower` shift by `'a'-'A'` only for the
relevant case. No locale, no signedness traps (args are `int`).

**`errno.h`.** Aliases (`EPERM`...`ENOEXEC`) for the shared `SYS_E*` numbers and
the `strerror` prototype. There is deliberately **no** `errno` lvalue: the kernel
returns `-errno` in `rax`, the wrapper returns it verbatim, and a caller writes
`if (rc == -ENOENT)` or `strerror(-rc)`. This avoids per-thread errno storage in
a freestanding runtime.

**`assert.h`.** `assert(expr)` expands to a `do{}while(0)` that, on a false
`expr`, `printf`s `assertion failed: <stringized expr> (<__FILE__>:<__LINE__>)`
and `exit(1)`. Defining `NDEBUG` before include collapses it to `((void)0)`.
Pulls in `stdio.h` + `stdlib.h`.

**`utest.h`.** The ring-3 test harness (mirrors kernel `ktest.h`):

| Macro | Effect |
| --- | --- |
| `UT_BEGIN()` | declares `int __ut_fail = 0` |
| `UT_CHECK(cond, msg)` | on false: print `[CHECK FAILED] <msg> (file:line)`, set flag |
| `UT_CHECK_EQ(a, b, msg)` | compares as `long`; prints got/want on mismatch |
| `UT_RESULT(marker)` | print `[PASS] <marker>` or `[FAIL] <marker>` |
| `UT_FAILED()` | read the flag (used for the program exit code) |

Because the verify targets grep for `[PASS] <marker>`, any failed check both
flips the result line to `[FAIL]` and (via `UT_FAILED()`) the process exit code.

## Edge cases

| Input | Result |
| --- | --- |
| `snprintf(buf, 4, "%d", 12345)` | writes `"123"`, NUL at index 3, returns 5 |
| `snprintf(NULL, 0, ...)` | counts only; returns the length, writes nothing |
| `strtol("   -0x1F", &e, 0)` | -31, `e` past the `F`; base auto-detected |
| `strtol("zzz", &e, 10)` | 0, `e == start` (nothing consumed) |
| `printf("%5d", 7)` | `"    7"` (space pad); `printf("%-5d", 7)` -> `"7    "` |
| `printf("%05x", 0x2a)` | `"0002a"` |
| `malloc` past 1 MiB | NULL (heap window exhausted) |
| `memmove(p+1, p, n)` overlap | correct (backward copy) |

## Invariants

- **Freestanding, no host headers.** Everything is defined in-tree; types come
  from `types.h`, not `stdint.h`/`stddef.h`. `printf` uses `__builtin_va_*`.
- **`snprintf` always NUL-terminates** when `size > 0` and returns the count that
  *would* have been written (truncation-safe); the terminator is placed at
  `min(n, size-1)`.
- **No global `errno`.** Error handling is by negated syscall return; `strerror`
  accepts either sign.
- **malloc alignment and layout.** Payloads are 16-byte aligned; every block is
  preceded by a 16-byte header holding its payload size. `realloc(NULL, n)` ==
  `malloc(n)`; `realloc(p, 0)` frees and returns NULL; `calloc` zeroes.
- **`free` is a no-op.** The bump allocator never reclaims; memory is released
  only when the process exits.
- **`fdgets` is byte-at-a-time** by design, to be safe on pipes and the console
  TTY where a bulk read could consume past the line.
- **`qsort` element bound.** Elements larger than 256 bytes are silently not
  sorted (the userland never sorts such elements).

## Failure modes

- **Out of heap.** `malloc` returns NULL once an allocation would cross
  `USER_HEAP_BASE + USER_HEAP_MAPPED` (1 MiB). `realloc` returns NULL if the new
  `malloc` fails (the old block is left intact); `calloc` propagates NULL.
- **`strtol` with no digits** returns 0 and sets `*endptr` back to the original
  start, so callers can detect "nothing parsed".
- **`assert` failure** terminates the process with exit code 1 (unless `NDEBUG`).
- **printf truncation** in `snprintf` is non-fatal: output is bounded, the return
  value still reports the full length.
- **Unknown `strerror` code** yields "Unknown error" rather than misbehaving.

## Verification

Build target (`Makefile.production`): **`verify-libc-expanded`**, expecting the
serial marker:

```
[PASS] libc runtime tests
```

emitted by `user/tests/libc_test.c` (spawned by `init` as
`/tests/libc_test.hxe`). The test uses `utest.h` and covers:

- **string:** `strlen`, `strcmp` (eq/lt), `strncmp` prefix, `strcpy`+`strcat`,
  `strchr`/`strrchr`/`strstr` offsets, `memset`/`memcpy`/`memcmp`, and a
  `memmove` overlap case (`memmove(m1+1, m1, 7)` then check the last byte).
- **stdlib:** `atoi` (positive/negative), `strtol("ff", 0, 16) == 255`, `abs`,
  `qsort` of 6 ints (min/max land correctly), `bsearch` for a present key.
- **ctype:** `isdigit`, `isalpha`, `toupper`/`tolower`, `isspace`.
- **stdio:** `snprintf(s, n, "%d-%s-%x", 42, "ok", 255)` produces `"42-ok-ff"`
  and returns 8.
- **malloc:** `malloc(16)` then `realloc(p, 64)` preserves the prior contents;
  `calloc(8, sizeof(int))` zeroes; `free` of both (no-op).

The program returns `UT_FAILED() ? 1 : 0`, so a regression both flips the exit
code (caught by `init`/`coreutils_test` callers) and drops the `[PASS]` marker
(caught by the verify grep).

## Future expansion

- **A reclaiming allocator.** Replace the bump heap with a real free-list (or
  back `malloc` with the `sbrk`/`mmap` wrappers that already exist in
  `unistd.c`); `free` is currently a no-op and the heap is capped at the
  pre-mapped 1 MiB window.
- **printf precision and `%f`.** No precision specifier and no floating point
  today (soft-float userland). Width-only formatting is supported.
- **Buffered stdio / `FILE`.** There is no `FILE`, `fopen`, `fread`/`fwrite`, or
  output buffering beyond the per-call 256-byte `printf` flush buffer.
- **`strtoul`/`strtoll` and a richer `<stdlib.h>`.** Only `strtol`/`atoi`/`atol`
  exist; unsigned/long-long parsers are a natural next step.
- **Locale / wide chars / UTF-8 classification.** ctype is ASCII-only.
