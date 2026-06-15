# Roadmap: from ~31k to 1,000,000 LOC

This is a technically-grounded plan for growing MyOS from its current size to a
million lines of *useful* source (the metric the build already gates on:
`make production-loc-check` counts `.c/.h/.S/.ld/.py/.md` under
`bootloader/ kernel/ user/ shared/ tools/ docs/`, excluding `build/`). It is tied
to the architecture that exists today, names the concrete subsystems to add, the
order forced by their dependencies, and a rough LOC estimate per area.

LOC is a *consequence*, not a goal: a real `ext`-like filesystem, a TCP/IP stack,
an SMP scheduler, a compositor, and a self-hosting toolchain port are each tens to
hundreds of thousands of lines because they are genuinely that large. The
estimates below are order-of-magnitude, drawn from the scale of comparable
real-world subsystems, and assume the same dense, tested, documented style as the
current tree (every subsystem ships `kernel/tests/*` suites and a `docs/*.md`).

## Architecture

Where we are today. Current useful LOC (from `make loc`): **~31,200 total** —
bootloader ~1.2k, kernel ~21.6k, user ~2.3k, shared ~0.06k, tools ~1.7k,
docs ~4.3k. Selected kernel subsystems: `arch/x86_64` ~1.7k, `memory` ~1.5k,
`fs` ~1.6k, `usb` ~2.3k, `storage` ~0.65k, `sched` ~0.5k, `input` ~0.57k.
`kernel/net/` exists only as an early **foundation** — a `struct netbuf`
skb/mbuf-style packet buffer (`netbuf.c/.h`), an Ethernet header (`ethernet.h`)
and endian helpers (`net_endian.h`), ~290 LOC with no protocol stack yet (the
`verify-network` / `verify-ethernet` … targets in `Makefile.production` are
reserved placeholders waiting on it). The interim gate is `PROD_LOC_MIN = 200000`
(`make verify-production-200k`); the 1M target is the long-horizon goal this
document orders the work toward.

What already exists to build on:

- UEFI bootloader (PE/COFF), ELF64 kernel, `boot_info` handoff.
- x86-64 arch: GDT/TSS/IDT/exceptions, PIC/APIC/IOAPIC/MADT, PIT + LAPIC timer.
- Memory: PMM, VMM (4-level paging), heap, slab, VM regions.
- Sync: spinlock, mutex, rwlock, semaphore, completion, waitqueue.
- Scheduler + threads + sleep; process model, syscalls, ring-3 loader (HXE1).
- VFS + ramfs + devfs + a persistent on-disk **HNXFS**; block layer + cache;
  partitions (MBR/GPT); AHCI + NVMe foundations.
- PCI + driver core + MSI/MSI-X; hardware event bus; driver lifecycle.
- Full USB stack (xHCI + core + HID) and a unified PS/2+USB input stack + TTY.
- Debug/trace framework, in-kernel test harness, host QEMU verifier, LOC gate.

These are the seams the roadmap extends; nothing below is a rewrite.

### Dependency ordering (the critical path)

```
   [now] arch + mem + sched + VFS + block + PCI + USB + input + test infra
      │
      ├─► SMP (per-CPU, locking audit)            ─┐
      │                                            ├─► everything scales/perf
      ├─► full VM (demand paging, mmap, swap, COW) ─┘
      │        │
      │        └─► POSIX process/signals ──► full libc ──► self-hosting toolchain
      │
      ├─► ACPI/AML interpreter ──► power mgmt, hotplug, SMP topology
      │
      ├─► TCP/IP completion ──► sockets ──► network services
      │
      ├─► ext-like FS + journaling ──► package manager (needs FS + net + libc)
      │
      └─► GPU/DRM + compositor + GUI toolkit ──► desktop apps
                                         audio stack ─┘
```

Two items are *force multipliers* and should come first because almost everything
else either depends on them or is reshaped by them: **SMP** (touches every lock
and per-CPU assumption) and the **full virtual-memory subsystem** (demand paging,
`mmap`, COW, swap — required by a real libc, a package manager, and large apps).

## File map

New top-level trees and the existing seams they extend. (Estimated LOC per area.)

| Planned tree | Extends today | Rough LOC |
|--------------|---------------|-----------|
| `kernel/smp/`, per-CPU in `kernel/arch` | `arch`, `apic`, `sched` | 25k |
| `kernel/memory/` (paging/swap/mmap) | `memory`, `user_fault` | 35k |
| `kernel/acpi/` (+ AML interpreter) | bootloader ACPI, `madt` | 30k |
| `kernel/net/` (netbuf foundation → full stack) | PCI, MSI, VFS | 150k |
| `kernel/fs/ext/`, `fs/fat/`, `fs/iso/` | `fs/vfs`, `block` | 70k |
| `kernel/gpu/` (DRM-like) + `kernel/video/` | framebuffer, PCI, VM | 90k |
| `kernel/sound/` | PCI, DMA | 30k |
| `kernel/virt/` (KVM-like) | VM, ACPI, SMP | 50k |
| `user/libc/`, `user/ld.so/` | `user/lib`, syscalls | 80k |
| `toolchain/` (self-hosting) | libc, FS | 80k |
| `user/services/`, `user/pkg/` | POSIX, net | 45k |

## Data structures

The growth is anchored by data structures the tree already has, generalized:

- **`struct vfs` / inode model** (`kernel/fs/vfs.c`) — already carries ramfs,
  devfs, HNXFS; ext-like FS, FAT, ISO9660, `/proc`, `/sys`, sockets-as-files and
  `/dev/input/*` all become new `struct filesystem` implementations.
- **`struct pci_device` + `struct driver`** (`kernel/pci`, `kernel/driver`) — NICs,
  GPUs, audio and storage controllers attach through the existing match/probe
  path; xHCI is the template register-mapped, ring-based driver.
- **`struct kobject` + handle tables** (`kernel/object`) — refcounts and
  handle-rights (verified in `debug_tests.c`) become the basis for a real
  user/group/capability model.
- **`struct usb_bus` HCD callbacks** — the same indirection generalizes to a
  block-device-operations / net-device-operations table for new transports.
- **`enum ktrace_cat`** (`NET`, `BLOCK`, `USB`, `DRIVER`, …) — categories already
  reserved for subsystems not yet written; new code emits `KTRACE` with no
  framework change.
- **`struct netbuf`** (`kernel/net/netbuf.h`) already provides the skb/mbuf
  packet buffer (head/data/tail/end with `netbuf_push/pull/put/reserve/trim` and
  stamped `eth`/`net`/`xport` layer pointers); the Tier 4 protocol layers build
  directly on it rather than introducing a parallel buffer type.
- **New core types** the tiers introduce: `struct cpu` (per-CPU area),
  `struct vm_area` (mmap regions), `struct socket` (over `netbuf`),
  `struct gem_object` (GPU buffers), AML namespace nodes.

## Key APIs

Each tier lands behind a small, stable kernel/user API surface:

- **SMP:** `smp_boot_aps()`, `this_cpu()`, `smp_call_function()`, per-CPU
  `percpu_get/set`, IPIs through the existing LAPIC layer.
- **Full VM:** `vm_mmap/munmap/mprotect`, a *recoverable* page-fault handler
  (extending `kernel/user/user_fault.c`), `swap_out/swap_in`, COW fork.
- **POSIX:** `fork`, `execve` (ELF), `sigaction`/`kill`, `poll`/`epoll`,
  `socket`/`bind`/`accept` — new entries in `kernel/user/syscall_numbers.h` and
  `syscall_table.c`.
- **Networking:** `netif_register()`, `ip_output()`, `tcp_connect()`, BSD sockets
  on top — mirroring the reserved `verify-ethernet/arp/ipv4/icmp/udp/sockets`
  markers in `Makefile.production`.
- **Filesystems:** new `struct filesystem` op tables (`ext_mount`, `fat_mount`,
  `nfs_mount`) plugging into `vfs_mount`.
- **Graphics/audio:** `drm_modeset()`, `gem_create()`, `snd_pcm_open()`.

## Invariants

Properties the roadmap must preserve as the tree grows:

- **Test-and-marker discipline scales.** Every new subsystem adds a
  `kernel/tests/<x>_tests.c` with `KT_CHECK`/`KT_RESULT` markers and a
  `make verify-<x>` target. The `Makefile.production` placeholders
  (`verify-network`, `verify-ethernet`, …, `verify-services`, `verify-stress`)
  are exactly these slots, already wired into `verify-production-200k`.
- **VFS is the single integration point** for anything file-shaped; no subsystem
  invents a parallel namespace.
- **Drivers attach through the existing bus/driver/MSI layers**; no driver pokes
  PCI config space or programs interrupts outside that path.
- **The LOC counted is useful LOC** — source, tests, docs, tools. Generated code
  and binaries stay out of the scanned trees, so the gate cannot be gamed.
- **Docs scale with code** (~1:8 today; docs are ~14% of kernel LOC). Each tier
  ships its `docs/*.md`.
- **Backward-compatible boot.** The Prompt 2–6 verify chain
  (`verify-boot` … `verify-prompt6`) keeps passing at every step.

## Failure modes

Risks that would derail the path, and how the architecture mitigates them:

- **SMP locking regressions.** The biggest hazard: every existing global
  (`g_xhci`, the input/log/trace rings, slab) assumes single-CPU access.
  Mitigation: do the lock audit *as part of* the SMP tier before scaling other
  subsystems, backed by `verify-stress`/`verify-randomized` under multiple vCPUs.
- **Unrecoverable faults.** Today every kernel CPU exception panics
  (`exceptions.c` → `kernel_panic`). Demand paging, COW and guard pages require
  returning from the page-fault handler; getting this wrong turns soft faults into
  panics. Mitigation: build the recoverable handler in the VM tier with dedicated
  fault-injection tests.
- **ABI drift.** The syscall ABI is shared via `kernel/user/syscall_numbers.h`;
  uncoordinated additions break userland. Mitigation: append-only numbering and a
  `verify-syscall-expanded`-style gate.
- **LOC-for-LOC-sake.** Padding to hit a number produces dead code that the test
  matrix won't cover. Mitigation: the gate counts only trees that ship with tests;
  coverage instrumentation (Tier 7) flags unexercised code.
- **Toolchain bootstrap stall.** Self-hosting depends on libc + dynamic linker
  being correct first; attempting it early fails. Mitigation: strict Tier 2
  ordering.

## Verification

The 1M effort is gated by the same marker-grep model as everything else:

- Interim: `make verify-production-200k` runs `verify-prompt6` plus every expanded
  suite and the QEMU memory matrix, then `production-loc-check` enforces
  `PROD_LOC_MIN = 200000`. Reached roughly at the end of Tier 1 + early Tier 2.
- Each tier flips its reserved `Makefile.production` targets from placeholder to
  real (e.g. `verify-network`, `verify-ethernet`, `verify-arp`, `verify-ipv4`,
  `verify-icmp`, `verify-udp`, `verify-dhcp`, `verify-dns`, `verify-sockets` for
  Tier 4; `verify-services`, `verify-stress`, `verify-randomized` for Tier 7).
- New `make verify-<x>` targets boot the real image under QEMU + OVMF and assert
  `[OK]`/`[PASS]` markers emitted by code that actually ran — no hardcoded passes.
- `make loc` / `make production-loc-check` track progress toward the LOC target at
  every step.

## Subsystem plan with LOC estimates

### Tier 1 — Foundations that reshape everything (~120k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| SMP bring-up | `arch`, APIC, sched | 25k | AP trampoline, per-CPU areas, IPIs, scheduler load-balancing, RCU-lite, full lock audit of every global. |
| Full VM (demand paging, mmap, COW, swap, guard pages) | `memory`, `user_fault` | 35k | Recoverable page-fault handler; backing store; reverse maps; `mmap`/`munmap`/`mprotect`; page reclaim + swap to a block device. |
| ACPI + AML interpreter | bootloader ACPI, MADT | 30k | Table parser + a real AML bytecode interpreter for `_PRT`, power, thermal, hotplug, SMP topology. |
| Scheduler maturity | `sched`, SMP | 15k | Priority classes, CFS-like fairness, cpusets/affinity, a real-time class, idle governors. |
| Time/clock subsystem | `time`, HPET, TSC | 15k | HPET, TSC calibration, high-res timers, a clocksource abstraction, an NTP-able monotonic clock. |

### Tier 2 — POSIX userland substrate (~210k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| POSIX process/signals/job control | `process`, full VM | 30k | `fork`/`execve` (ELF, replacing the HXE1-only loader), signals, process groups, sessions, `wait` semantics, ptrace. |
| Full libc | syscalls, full VM | 60k | Replace `user/lib/*` with a complete C library: stdio, malloc (arena/tcache), pthreads, locale, math, `<dlfcn.h>`. |
| ELF dynamic linking + shared libs | libc, full VM | 20k | `ld.so`, GOT/PLT, `LD_LIBRARY_PATH`, versioned symbols. |
| Self-hosting toolchain port | libc, ld.so, FS | 80k | Port a C compiler + assembler + linker + make so the OS builds itself — the largest single line item and the milestone that proves the platform. |
| Coreutils + shell expansion | current `user/` | 20k | Grow `user/coreutils` and the shell into a POSIX `sh` with pipes, redirection, job control, globbing, scripting. |

### Tier 3 — Storage & filesystems (~130k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| ext-like journaling FS | VFS, block cache, HNXFS | 40k | Inodes/extents, block groups, an ordered/writeback journal, `fsck`, online resize. |
| FS feature breadth | VFS | 30k | FAT32 (UEFI interop), ISO9660, a network FS (9p/NFS), tmpfs maturity, overlayfs, quotas, ACLs/xattrs. |
| Block layer maturity | `block`, NVMe/AHCI | 25k | I/O scheduler, request merging, multi-queue, TRIM/discard, write barriers, dm-style RAID/crypt/snapshots. |
| Storage drivers | PCI, MSI, xHCI bulk | 20k | Finish NVMe (namespaces, IRQ-driven queues), USB mass storage, virtio-blk, SD/MMC. |
| Page/buffer cache unification | VM, block | 15k | A unified page cache shared by mmap + read/write; writeback threads; readahead. |

### Tier 4 — Networking (~150k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| Link + L3/L4 core | PCI, MSI, `kernel/net` netbuf | 50k | NIC drivers (virtio-net, e1000), Ethernet (header type exists), ARP, IPv4/IPv6, ICMP, UDP, a real TCP (congestion control, retransmit, SACK) — built on the existing `netbuf` push/pull/put API. |
| Sockets + BSD API | net core, syscalls, VFS | 30k | `socket`/`bind`/`listen`/`accept`/`poll`/`epoll`, socket buffers, netlink-style control. |
| Net services | sockets, libc | 30k | DHCP, DNS resolver, NTP, an HTTP client/server, SSH. |
| Routing + firewall + namespaces | net core | 25k | Routing tables, netfilter-style hooks, traffic shaping, network namespaces. |
| Crypto / TLS | libc, big-int | 15k | Symmetric/asymmetric primitives and a TLS stack for the package manager and remote services. |

### Tier 5 — Graphics, audio, HID breadth (~200k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| GPU / DRM-like layer | PCI, MSI, VM | 50k | Mode-setting, framebuffer management, GEM-style buffer objects, virtio-gpu, the UEFI GOP fallback retained. |
| Compositor + window system | DRM, input | 40k | A display server consuming the unified `input_event` stream, damage tracking, multi-window, a Wayland-like protocol. |
| GUI toolkit + apps | compositor, libc | 50k | Widgets, text/font rendering, a terminal emulator, file manager, editor, settings. |
| Audio stack | PCI, DMA | 30k | HD-Audio/AC'97/virtio-snd drivers, a mixing/routing server, a PCM API. |
| Input breadth | `input`, USB HID | 30k | Report-protocol HID, gamepads/tablets/touch, real PS/2 mouse, multiple VTs, an evdev-style interface, keymap layers. |

### Tier 6 — Drivers, virtualization, platform (~180k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| Driver model maturity | `driver`, PCI, ACPI | 25k | A device-tree/sysfs hierarchy, hotplug, power domains, deferred probe, firmware loading. |
| Bus + transport drivers | PCI, USB, ACPI | 40k | virtio family, USB hubs + bulk/iso + UAS, SATA breadth, PCIe hotplug, I2C/SPI/GPIO. |
| Virtualization | VM, ACPI, SMP | 50k | virtio guest drivers plus a KVM-like hypervisor (VMX, EPT, vCPU scheduling). |
| Containers / isolation | namespaces, cgroups | 25k | PID/mount/net namespaces, cgroups (cpu/mem/io), seccomp-style syscall filtering. |
| Power management | ACPI/AML | 20k | S-states, CPU freq/idle governors, device runtime PM, battery/thermal. |
| Security / capabilities | process, VFS, handles | 20k | Users/groups, capabilities atop the `kobject` handle-rights model, a MAC layer, audit. |

### Tier 7 — System integration & ecosystem (~110k LOC)

| Subsystem | Builds on | Rough LOC | Notes |
|-----------|-----------|-----------|-------|
| init / service manager | POSIX, sockets | 20k | Dependency-ordered service supervision (the reserved `verify-services` marker). |
| Package manager | FS, net, TLS, libc | 25k | Repos, dependency resolution, signed packages, transactional install/rollback. |
| Test infra expansion | current harness | 25k | `verify-stress`, `verify-randomized`, fuzzing, coverage, KVM-accelerated CI. |
| Documentation | all | 30k | Per-subsystem `docs/*.md` continues to grow ~1:8 with code. |
| Tools | `tools/` | 10k | Image/FS/USB/PCI inspectors, profilers, trace decoders, crash analyzers. |

### Running total

| Tier | LOC |
|------|-----|
| Current | ~31k |
| Tier 1 — Foundations (SMP, full VM, ACPI/AML, sched, time) | ~120k |
| Tier 2 — POSIX userland (process/signals, libc, ld.so, toolchain, sh) | ~210k |
| Tier 3 — Storage/FS (ext journaling, FAT/ISO/NFS, block maturity, drivers, cache) | ~130k |
| Tier 4 — Networking (L2–L4, sockets, services, routing/fw, crypto/TLS) | ~150k |
| Tier 5 — Graphics/audio/input (DRM, compositor, toolkit, audio, HID breadth) | ~200k |
| Tier 6 — Drivers/virt/platform (driver model, buses, KVM, containers, PM, security) | ~180k |
| Tier 7 — Integration/ecosystem (init, package mgr, test infra, docs, tools) | ~110k |
| **Projected total** | **~1.13M** |

The tiers overshoot 1M deliberately: real subsystems come in larger than first
estimates, and the LOC gate counts tests and docs (which scale with code).

## Future expansion

Beyond the 1M milestone, the architecture points at further work that the tiers
above set up but do not complete:

- **Additional ISAs** — an `arch/aarch64` and `arch/riscv64` port, factoring the
  arch-specific seams (`gdt`/`idt`/paging) the x86-64 tree already isolates.
- **A microkernel-style IPC layer** atop the `kobject`/handle model for
  user-space drivers and services.
- **Cluster / distributed features** — once networking and virtualization land,
  live migration of the KVM-like guests and distributed filesystems.
- **Formal verification of core invariants** (the scheduler, the VM page tables)
  using the already-explicit invariants documented per subsystem.
- **A reproducible, self-bootstrapping build** where the ported toolchain rebuilds
  the entire tree under MyOS itself, closing the self-hosting loop.

## Sequencing summary

1. **Tier 1 first** — SMP and full VM unblock and reshape everything; ACPI/AML
   unblocks SMP topology, hotplug, and power management.
2. **Tier 2 next** — POSIX + libc + dynamic linking are prerequisites for the
   self-hosting toolchain, the package manager, and most real applications.
3. **Tiers 3 and 4 in parallel** — storage/FS and networking are largely
   independent once VFS/block and PCI/MSI are mature, so they can advance
   concurrently.
4. **Tier 5** — graphics/audio depend on mature VM (buffer objects), DRM-capable
   drivers, and the already-unified input stack.
5. **Tiers 6 and 7** — driver breadth, virtualization, containers, and the
   ecosystem (init, package manager, CI) integrate the lower tiers into a usable,
   self-maintaining system.

The 200k interim gate (`verify-production-200k`) is reached around the end of
Tier 1 plus the early parts of Tier 2; the remaining tiers carry the tree past
1M useful LOC while keeping the Prompt 2–6 verify chain green at every step.
