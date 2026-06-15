# Shell

The MyOS shell (`user/shell/`) is a ring-3 program that reads command lines,
tokenizes them, expands `$VAR` references, runs in-process builtins, and
otherwise resolves a command to `/bin/<cmd>.hxe` and `spawn`s it. It runs in two
modes — a scripted mode used by the boot test matrix and an interactive mode with
a prompt — both reading one line at a time from stdin (`/dev/console`). It is a
freestanding program linked against the user libc (see `docs/libc.md`) and the
syscall layer (see `docs/userland.md`).

It is built from three translation units plus their headers:

| File | Role |
| --- | --- |
| `user/shell/shell.c` | `main`, the read/run loop, `$VAR` expansion, command resolution |
| `user/shell/parser.c` / `parser.h` | the whitespace + double-quote tokenizer |
| `user/shell/builtins.c` / `builtins.h` | builtins + the variable table + the history ring |

## Architecture

```
   main()  --mode select (-i?)--+
                                |
        +----- read_line(stdin, 1 byte at a time, up to '\n'/EOF) <----+
        |                       |                                      |
        |              shell_history_record(line)                      |
        |                       |                                      |
        |             run_line(line, &want_exit)                       |
        |             /         |          \                           |
        |   parse_line()   $VAR expand   builtin_try() --yes--> done   |
        |        |              |              |no                      |
        |     argv[]        argv[i] = var   resolve_path()             |
        |                                       |                       |
        |                              spawn(/bin/<cmd>.hxe, argv)      |
        |                                       |                       |
        |                                  wait_pid(pid, &code)         |
        +---------------------------------------+----------------------+
                                |
                   want_exit -> print [PASS] marker, return 0
```

Builtins that must mutate shell-process state (cwd, variables, history, or
requesting exit) run **in-process**; external commands run in a child via
`spawn`/`wait_pid`. The shell keeps three pieces of in-process state in
`builtins.c`: a variable table, a history ring, and (transiently) the parsed
argv.

## File map

| File | Contents |
| --- | --- |
| `user/shell/shell.c` | `read_line`, `resolve_path`, `run_line`, `main` (scripted vs interactive) |
| `user/shell/parser.h` | `SHELL_MAX_ARGS = 16`; `parse_line` declaration |
| `user/shell/parser.c` | `parse_line`: in-place whitespace tokenizer with `"..."` support |
| `user/shell/builtins.h` | sizes (`SHELL_MAX_VARS=24`, etc.); builtin + var/history decls |
| `user/shell/builtins.c` | variable table, history ring, `which_path`, `run_selftest`, `builtin_try` |

The shell is listed in the Makefile `USER_BIN` set and installed as
`/bin/shell.hxe`; `init` runs it scripted then interactive (`shell -i`).

## Data structures

**The tokenizer (`parser.c`).** `parse_line(char *line, char *argv[], int max)`
splits `line` **in place**: it walks the buffer, skips spaces/tabs/`\r`, and for
each token either reads a double-quoted span (advancing past the opening `"`,
terminating at the closing `"` which it overwrites with NUL) or a whitespace-
delimited word (NUL-terminating at the next space). It writes at most `max-1`
args, NUL-terminates `argv[argc]`, and returns `argc`. There is no escape
processing, no single quotes, and no nested quoting.

**The variable table (`builtins.c`).** Three parallel fixed arrays:

```c
#define SHELL_MAX_VARS   24
#define SHELL_VAR_NAME   32
#define SHELL_VAR_VALUE  96
static char g_var_name[SHELL_MAX_VARS][SHELL_VAR_NAME];
static char g_var_value[SHELL_MAX_VARS][SHELL_VAR_VALUE];
static int  g_var_count;
```

`var_find(name)` linear-scans for a name. `shell_var_set` overwrites in place if
present, else appends (silently dropping the set when the table is full).
`shell_var_get` returns the value pointer or NULL. `shell_var_unset` removes by
swapping the last entry into the hole (compaction; order is not preserved).

**The history ring (`builtins.c`).**

```c
#define SHELL_HISTORY   32
#define SHELL_HIST_LEN  128
static char g_hist[SHELL_HISTORY][SHELL_HIST_LEN];
static int  g_hist_count;   /* total ever recorded */
```

`shell_history_record(line)` ignores empty lines, stores into slot
`g_hist_count % SHELL_HISTORY` (overwriting the oldest once full), and increments
the total. `history_print` lists from `max(0, total - SHELL_HISTORY)` up to
`total`, numbering each `1`-based and indexing the ring modulo `SHELL_HISTORY`.

## Tokenizer, step by step

`parse_line` is a single forward scan over the mutable line buffer. Walking
`p` from the start, for each token (while `argc < max-1`):

1. skip `is_space` chars (`' '`, `'\t'`, `'\r'`);
2. if at end-of-string, stop;
3. if the next char is `"`, advance past it, record `argv[argc++] = p`, scan to
   the closing `"` (or end), and if a closing quote is found overwrite it with a
   NUL and advance;
4. otherwise record `argv[argc++] = p`, scan to the next space, and if not at end
   overwrite that space with a NUL and advance.

Finally `argv[argc] = NULL` and `argc` is returned.

Worked example — input ``set MSG "hello world"``:

```
   line: s e t \0 M S G \0 " h e l l o   w o r l d "
                                ^ quote consumed, closing " -> \0
   argv[0] = "set"
   argv[1] = "MSG"
   argv[2] = "hello world"   (the embedded space survives the quotes)
   argv[3] = NULL   ->   argc = 3
```

Limits: at most `SHELL_MAX_ARGS-1 = 15` tokens; no escapes, no single quotes, no
`${}`; an unterminated quote runs to end-of-line.

## Key APIs

**`builtins.h`**
- `int builtin_try(int argc, char *argv[], int *want_exit)` — if `argv[0]` is a
  builtin, handle it and return 1 (setting `*want_exit` for `exit`); else 0.
- `void shell_var_set/​unset(...)`, `const char *shell_var_get(name)`.
- `void shell_history_record(line)`, `int shell_history_count()`.

**`parser.h`**
- `int parse_line(char *line, char *argv[], int max)`.

**`shell.c` internals**
- `read_line(buf, max)` — reads stdin one byte at a time, stops at `\n` (consumed,
  not stored) or buffer limit; returns -1 at EOF with nothing read, else the
  length.
- `resolve_path(cmd, out, out_size)` — if `cmd` contains `/`, copy it verbatim;
  otherwise build `"/bin/" + cmd + ".hxe"`.
- `run_line(line, want_exit)` — parse, `$VAR`-expand, try builtins, else resolve
  and `spawn`/`wait_pid`.

## `$VAR` expansion

Expansion happens in `run_line` **after** tokenizing and **before** dispatch.
For each token, if `argv[i][0] == '$'`, the shell looks up the name (`argv[i]+1`)
in the variable table; if found, it replaces the argv slot with the variable's
value pointer. Notes on the exact behavior:

- Expansion is **whole-token only**: a token must *be* `$NAME`. There is no
  in-string interpolation (`"$x/y"` is not expanded), and `${NAME}` syntax is not
  supported.
- An undefined `$NAME` is left as the literal `$NAME` (the slot is unchanged).
- Expansion reads only the shell's own variable table (set via `set`/`export`),
  not the process environment directly — though `export` mirrors a variable into
  both (see below).

## Builtins

Dispatched by `builtin_try` (string-compared against `argv[0]`):

| Builtin | Behavior |
| --- | --- |
| `exit` | sets `*want_exit = 1`; the loop ends and the mode marker prints |
| `cd [dir]` | `chdir(dir)` (default `/`); prints `cd: <dir>: no such directory` on failure |
| `pwd` | `getcwd` then print |
| `set NAME VALUE` | `shell_var_set`; `set NAME` prints `NAME=value` (empty if unset) |
| `unset NAME` | `shell_var_unset` |
| `export NAME=VALUE` | splits on `=`, sets the shell var **and** `env_set(NAME)` into the process environment |
| `history` | prints the recorded history ring (numbered) |
| `which NAME` | `which_path(NAME)`: prints the resolved `/bin/<cmd>.hxe` path, or `NAME: not found` |
| `help` | prints `builtins: cd pwd set unset export history which help selftest exit` |
| `selftest` | runs `run_selftest()` (drives var/history/which machinery) |

`which_path(cmd, out, max)` mirrors `resolve_path` (verbatim if `cmd` has a `/`,
else `/bin/<cmd>.hxe`), then `open(out, O_RDONLY)` to confirm existence and closes
the fd. The `which` builtin and the `selftest` both use it.

## Builtin behavior detail

Each branch in `builtin_try` (string-compared against `argv[0]`):

- **`set`** — `set NAME VALUE` (argc ≥ 3) stores; `set NAME` (argc == 2) prints
  `NAME=value` (empty string if unset). Nothing with argc < 2.
- **`unset`** — `unset NAME` (argc ≥ 2) removes via swap-with-last compaction.
- **`export`** — `export NAME=VALUE` finds the `=`, temporarily NUL-terminates the
  name, `shell_var_set(NAME, VALUE)`, restores the `=`, then `env_set(NAME)` to
  mirror the assignment into the process environment. A bare `export NAME` with no
  `=` does nothing.
- **`cd`** — `chdir(argv[1] or "/")`; prints `cd: <dir>: no such directory` on a
  negative result.
- **`pwd`** — `getcwd` into a 128-byte buffer, then print.
- **`history`** — `history_print()` (numbered, ring-bounded).
- **`which`** — `which_path(argv[1])`; prints the resolved path or
  `<name>: not found`.
- **`help`** — prints the fixed builtin list.
- **`selftest`** — `run_selftest()` (drives the variable/history/which machinery
  and prints the `[PASS] shell expanded tests` marker).
- **`exit`** — sets `*want_exit = 1`.

A non-matching `argv[0]` returns 0, signalling `run_line` to resolve and spawn.

## Command resolution

For a non-builtin command, `run_line`:

1. `resolve_path(argv[0], path, sizeof path)`:
   - contains `/` -> used as-is (an explicit path like `/disk/foo.hxe`);
   - otherwise -> `/bin/<argv[0]>.hxe`.
2. `spawn(path, argv)`. On a negative pid it prints
   `shell: command not found: <argv[0]>` and returns.
3. `wait_pid(pid, &code)` — the shell blocks until the child exits (the exit code
   is read but not currently surfaced as `$?`).

## Scripted vs interactive modes

`main` selects interactive mode when `argv[1]` begins with `-i`
(`argv[1][0]=='-' && argv[1][1]=='i'`). Both modes use the same read/run loop;
they differ only in framing and the final marker:

| | Scripted (`shell`) | Interactive (`shell -i`) |
| --- | --- | --- |
| Start banner | `[shell] scripted session start` | `[shell] interactive session start` |
| Prompt | none; echoes each line as `myos$ <line>` | `myos:<cwd>$ ` (cwd via `getcwd`, fallback `/`) |
| Input | one line at a time from stdin | one cooked line from the canonical TTY |
| End marker | `[PASS] shell scripted session` | `[PASS] shell interactive smoke` |

Because both read **one line at a time** from the same `/dev/console`, the
scripted shell stops at its own `exit` and leaves any following lines for the
interactive shell that `init` launches next. An empty line (`read_line` returns
0) is `continue`d before `run_line` and before `shell_history_record`, so only
non-empty lines are recorded and run.

## Session shapes

**Scripted (`/bin/shell.hxe`, no args).** Used by the boot test matrix. The shell
prints `[shell] scripted session start`, then for each input line echoes
`myos$ <line>`, records it, and runs it. Because reads are line-at-a-time from the
shared console, the scripted shell consumes lines up to and including its own
`exit`, then prints `[PASS] shell scripted session` and returns 0 — leaving any
remaining console input for the interactive shell `init` launches next.

```
[shell] scripted session start
myos$ help
builtins: cd pwd set unset export history which help selftest exit
myos$ selftest
[PASS] shell expanded tests
myos$ exit
[PASS] shell scripted session
```

**Interactive (`/bin/shell.hxe -i`).** The shell prints `[shell] interactive
session start`, then loops: build `myos:<cwd>$ ` (cwd from `getcwd`, `/` on
failure), read one cooked line from the canonical TTY, record it, run it. On EOF
it prints `[PASS] shell interactive smoke` and returns 0.

```
[shell] interactive session start
myos:/$ pwd
/
myos:/$ cd /bin
myos:/bin$ exit
[PASS] shell interactive smoke
```

## The `selftest` builtin

`run_selftest()` exercises the expanded-shell machinery and is the source of the
`verify-shell-expanded` marker:

- sets `FOO=bar`, overwrites to `baz`, then unsets and confirms each step;
- records a history line and confirms `shell_history_count()` increments;
- `which_path("echo")` succeeds (`/bin/echo.hxe` exists) and
  `which_path("definitely_not_a_cmd")` fails;
- prints `[PASS] shell expanded tests` (or `[FAIL] ...`).

## Reading input

`read_line(buf, max)` (`shell.c`) reads stdin one byte at a time with
`read(0, &c, 1)` until it sees `\n` (consumed, not stored), the buffer fills
(`max-1`), or EOF. It returns -1 when EOF arrives with nothing read, otherwise the
number of bytes stored (0 for an empty line). Byte-at-a-time reads keep the
scripted and interactive shells from over-consuming the shared console: each shell
takes exactly the lines up to its `exit`, leaving the rest for the next one. Both
the line buffer (`line[256]`) and the cwd buffer (`cwd[128]`) are fixed stack
arrays in `main`.

The loop body, per iteration: (interactive) build and print the prompt; read a
line; on EOF break; (scripted) echo `myos$ <line>`; skip empty lines; record into
history; `run_line`. The loop ends when a builtin sets `want_exit` or `read_line`
returns -1.

## Invariants

- **In-place tokenization.** `parse_line` mutates the input line (inserting NULs);
  the line buffer must outlive the argv it produces. `argv` is always
  NUL-terminated at `argv[argc]`.
- **Builtins run in the shell process.** Anything that changes cwd, variables,
  history, or requests exit must be a builtin; external programs cannot affect
  shell state.
- **`$VAR` is whole-token, table-only, leave-on-miss.** See the expansion section.
- **Bounded state.** At most 16 args/line, 24 variables (names ≤31, values ≤95
  chars), 32 history entries (≤127 chars each). Overflows are dropped/truncated,
  never overrun (`strncpy` + explicit NUL).
- **Resolution rule.** A name without `/` always becomes `/bin/<name>.hxe`; a name
  with `/` is taken literally.

## Failure modes

- **Unknown command** -> `shell: command not found: <name>` (spawn returned < 0).
- **`cd` to a missing directory** -> `cd: <dir>: no such directory` (chdir < 0).
- **`which` miss** -> `<name>: not found`.
- **Variable-table full** -> `set`/`export` silently drop the new variable.
- **Long input** -> `read_line` stops at `max-1` (256-byte line buffer); excess
  bytes are read on the next iteration.
- **EOF** -> `read_line` returns -1 with nothing read; the loop breaks and the
  mode marker prints.

## Verification

Build target (`Makefile.production`): **`verify-shell-expanded`**, expecting:

```
[PASS] shell expanded tests
```

produced by the `selftest` builtin (`run_selftest` in `builtins.c`). The shell is
launched twice by `init` — scripted (`/bin/shell.hxe`) then interactive
(`shell -i`) — which additionally surface `[PASS] shell scripted session` and
`[PASS] shell interactive smoke` on the serial log. The scripted boot session is
what drives `selftest` (and the other builtins) so the expanded-tests marker
appears.

## Future expansion

- **`$?` and richer expansion.** The child exit code from `wait_pid` is read but
  not exposed as `$?`; in-string interpolation and `${NAME}` are unsupported.
- **Pipelines, redirection, background jobs.** `pipe`/`dup2`/`setpgid`/`setsid`
  wrappers exist (see `docs/userland.md`) but the shell does not yet build
  pipelines, redirect fds, or manage `&` jobs (`jobs` reads `ps` instead).
- **Quoting/escapes.** Only simple double quotes; no single quotes, no `\`
  escapes, no quote concatenation.
- **A `PATH` variable.** Resolution is hardwired to `/bin`; honoring an
  environment `PATH` would generalize it.
- **Line editing/history recall.** History is recorded and printable, but there
  is no up-arrow recall or editing (the interactive line is whatever the
  canonical TTY hands back).
