# MyOS Scale Policy — Million-Line Production OS Direction

This project is not a toy OS.

This project is a from-scratch x86-64 desktop/server operating system intended to grow into a multi-million-line codebase through real functionality, not artificial bloat.

The codebase should eventually reach millions of useful lines because it includes:

```text
kernel architecture
memory management
scheduler
process model
syscalls
VFS
filesystems
device drivers
PCI
USB
storage
networking
graphics
GUI
window server
audio
security
package system
developer tools
debugger
compiler/toolchain
shell
core utilities
services
tests
docs
hardware compatibility layers
```

## Important Scale Principle

Do not inflate code with useless boilerplate.

The goal is:

```text
millions of dense, useful, maintainable lines
```

not:

```text
millions of duplicated, fake, low-value lines
```

The target is a compact but massive OS.

Think of it like this:

```text
Good architecture: 2M–10M useful lines
Bad architecture: 50M–80M bloated lines
```

Therefore, every prompt must now build more than a tiny proof of concept.

Each prompt should produce:

```text
1. real subsystem architecture
2. multiple modules
3. public APIs
4. internal tests
5. diagnostics
6. debug tools
7. documentation
8. verification targets
9. multiple real features
10. future expansion points
```

## New Prompt Philosophy

Old style:

```text
Build the smallest thing that proves the concept.
```

New style:

```text
Build the first serious version of the subsystem.
Make it broad enough to become production architecture.
Do not stop at a single happy-path demo.
```

Each prompt must avoid these phrases as implementation goals:

```text
minimal
tiny
stub only
placeholder
single demo only
toy
proof only
```

Acceptable only when explicitly describing temporary limitations.

## Required Prompt Behavior

Each future prompt must instruct the coding agent to:

```text
- expand the subsystem substantially
- create real directory structure
- create reusable APIs
- create tests and verification
- create docs
- create debug/inspection tooling
- create multiple examples or programs
- keep code dense and low-duplication
- avoid fake generated filler
- preserve all previous verification
```

## Line Count Expectations

Line count is not the main quality metric, but prompts should now intentionally create larger subsystem foundations.

Expected rough growth:

```text
Prompt 1–3 boot/kernel foundation:
5k–15k lines

Prompt 4 userland + syscall + VFS + shell foundation:
20k–60k+ lines

Prompt 5 process/filesystem/device foundation:
50k–150k+ lines

Prompt 6 PCI/storage/USB/input:
100k–300k+ lines

Prompt 7 networking stack:
100k–300k+ lines

Prompt 8 GUI/window server:
200k–700k+ lines

Prompt 9 developer tools/package system:
200k–800k+ lines

Long-term:
millions of useful lines
```

These are not artificial quotas. They are scale expectations caused by building real subsystem breadth.

## New Prompt 4 Direction

Prompt 4 must no longer be only:

```text
ring 3 + one user program
```

Prompt 4 must now be:

```text
Userland Foundation Mega-Phase
```

It should include:

```text
ring 3 transition
syscall ABI
user address spaces
user executable format
initramfs
basic VFS
devfs
file descriptors
console device
initial TTY abstraction
process/task model v0
spawn/exec/wait/exit v0
user runtime library
init process
shell v0
multiple core utilities
multiple user test programs
syscall tests
user fault isolation
verification matrix
documentation
```

Prompt 4 should build the first serious user/kernel boundary and userland layer, not just one hello-world user program.

## Verification Rule

Large scale does not mean unverified.

Every large prompt must still end with:

```text
make clean
make all
make image
make verify-previous
make verify-new-subsystem
make verify-qemu-matrix
make debug
```

If verification becomes too slow, add targeted verification modes instead of removing verification.

## Code Quality Rule

Large code must still be dense.

Prefer:

```text
shared abstractions
tables
small reusable helpers
clear subsystem APIs
state machines
well-defined structs
well-documented invariants
```

Avoid:

```text
copy-paste implementations
fake wrappers
unused abstractions
random over-engineering
hard-coded one-off hacks
massive files with unrelated logic
```

## Final OS Direction

The final OS should eventually include:

```text
kernel
drivers
filesystems
network stack
GUI
window server
shell
coreutils
services
package manager
compiler/toolchain
debugger
tests
installer
documentation
```

The project must grow toward this direction every phase.
