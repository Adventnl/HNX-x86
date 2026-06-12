# MyOS — x86-64 UEFI boot pipeline (stages 1-16) + kernel foundation (Prompt 2/2.5)
#
# Targets:
#   all bootloader kernel image run debug clean
#   inspect loc run-headless
#   verify-boot verify-exception verify-pagefault verify-qemu-matrix
#
# Destructive test flags (never set in a normal build; only by verify-*):
#   MYOS_TEST_INVALID_OPCODE  trigger #UD after init
#   MYOS_TEST_PAGE_FAULT      trigger #PF after init
#   MYOS_TEST_PMM_STRESS      extended PMM stress in early tests
#   MYOS_TEST_VERBOSE         extra diagnostic logging

# ---- Toolchain -------------------------------------------------------------
CC        := clang
LD_EFI    := lld-link
LD_KERNEL := ld.lld
PY        := python3

# Inspection tools (prefer llvm-*, fall back to GNU, then Homebrew llvm).
LLVMBIN := /opt/homebrew/opt/llvm/bin
READELF := $(shell command -v llvm-readelf 2>/dev/null || command -v readelf 2>/dev/null || echo $(LLVMBIN)/llvm-readelf)
NM      := $(shell command -v llvm-nm 2>/dev/null || command -v nm 2>/dev/null || echo $(LLVMBIN)/llvm-nm)
OBJDUMP := $(shell command -v llvm-objdump 2>/dev/null || command -v objdump 2>/dev/null || echo $(LLVMBIN)/llvm-objdump)

# ---- Directories -----------------------------------------------------------
BUILD  := build
B_BOOT := $(BUILD)/bootloader
B_KERN := $(BUILD)/kernel
B_IMG  := $(BUILD)/image
B_USER := $(BUILD)/user

# ---- Flags -----------------------------------------------------------------
BOOT_CFLAGS := --target=x86_64-unknown-windows -ffreestanding -fshort-wchar \
               -mno-red-zone -fno-stack-protector -fno-builtin -Wall -Wextra \
               -Ibootloader/include -Ishared/include
BOOT_LDFLAGS := /subsystem:efi_application /entry:EfiMain /nodefaultlib

KERN_INCLUDES := -Ikernel/include -Ikernel/arch/x86_64 -Ikernel/memory \
                 -Ikernel/sched -Ikernel/tests -Ikernel/initramfs \
                 -Ikernel/user -Ishared/include
# EXTRA_KCFLAGS is injected by the verify-* targets to enable destructive tests.
EXTRA_KCFLAGS ?=
# -mno-sse/-mno-mmx/-msoft-float: IRQ stubs save only the GPRs, so the kernel
# must never touch FP/SIMD state (an async timer IRQ would corrupt it).
KERN_CFLAGS := --target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
               -fno-builtin -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
               -mno-sse -mno-sse2 -mno-mmx -msoft-float \
               -nostdlib -Wall -Wextra $(KERN_INCLUDES) $(EXTRA_KCFLAGS)
KERN_ASFLAGS := --target=x86_64-unknown-none-elf $(KERN_INCLUDES)
KERN_LDFLAGS := -T kernel/arch/x86_64/linker.ld

# ---- User-space program flags (ring 3, freestanding, no libc) --------------
# Shares the kernel's syscall_numbers.h via -Ikernel/user so the ABI never
# drifts. Linked at USER_IMAGE_BASE with 4 KiB page granularity (ld.lld's
# x86-64 default is 2 MiB) and converted to HXE1 by tools/mkhxe.py.
USER_INCLUDES := -Iuser/lib -Ikernel/user
USER_CFLAGS := --target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
               -fno-builtin -fno-pic -fno-pie -mno-red-zone -mno-sse -mno-mmx \
               -msoft-float -nostdlib -Wall -Wextra $(USER_INCLUDES)
USER_ASFLAGS := --target=x86_64-unknown-none-elf $(USER_INCLUDES)
USER_LDFLAGS := -T user/linker.ld -z max-page-size=0x1000

# ---- Sources / objects -----------------------------------------------------
BOOT_SRC := $(wildcard bootloader/src/*.c)
BOOT_OBJ := $(patsubst bootloader/src/%.c,$(B_BOOT)/%.obj,$(BOOT_SRC))
BOOT_HDR := $(wildcard bootloader/include/*.h) shared/include/boot_info.h

# Kernel C/asm sources span src/, arch/x86_64/, memory/, tests/. Object file
# names are flattened into build/kernel/ (basenames are unique across dirs).
KERN_CSRC := $(wildcard kernel/src/*.c) $(wildcard kernel/arch/x86_64/*.c) \
             $(wildcard kernel/memory/*.c) $(wildcard kernel/sched/*.c) \
             $(wildcard kernel/tests/*.c) $(wildcard kernel/initramfs/*.c) \
             $(wildcard kernel/user/*.c)
KERN_SSRC := $(wildcard kernel/arch/x86_64/*.S) $(wildcard kernel/sched/*.S) \
             $(wildcard kernel/user/*.S)
KERN_OBJ  := $(addprefix $(B_KERN)/,$(notdir $(KERN_CSRC:.c=.o))) \
             $(addprefix $(B_KERN)/,$(notdir $(KERN_SSRC:.S=.o)))
KERN_HDR  := $(wildcard kernel/include/*.h) $(wildcard kernel/arch/x86_64/*.h) \
             $(wildcard kernel/memory/*.h) $(wildcard kernel/sched/*.h) \
             $(wildcard kernel/tests/*.h) $(wildcard kernel/initramfs/*.h) \
             $(wildcard kernel/user/*.h) shared/include/boot_info.h

vpath %.c kernel/src kernel/arch/x86_64 kernel/memory kernel/sched kernel/tests \
          kernel/initramfs kernel/user
vpath %.S kernel/arch/x86_64 kernel/sched kernel/user

# ---- User-space sources / objects ------------------------------------------
USER_LIB_OBJ := $(B_USER)/syscall.o $(B_USER)/string.o $(B_USER)/stdlib.o
USER_CRT0    := $(B_USER)/crt0.o
USER_PROGS   := $(B_USER)/init.hxe $(B_USER)/syscall_test.hxe
USER_HDR     := $(wildcard user/lib/*.h) kernel/user/syscall_numbers.h
INITRAMFS    := $(B_IMG)/initramfs.hxf

BOOTX64 := $(B_BOOT)/BOOTX64.EFI
KERNEL  := $(B_KERN)/kernel.elf
IMAGE   := $(B_IMG)/myos.img

# ---- Tool-presence guard ---------------------------------------------------
# Usage: $(call need,<tool>,<install hint without commas>)
define need
@command -v $(1) >/dev/null 2>&1 || { printf '\n[DEP MISSING] required tool "%s" not found on PATH.\n  %s\n\n' "$(1)" "$(2)"; exit 1; }
endef

# ---- Phony targets ---------------------------------------------------------
.PHONY: all bootloader kernel user initramfs image run debug clean dirs \
        inspect loc run-headless \
        verify-boot verify-exception verify-pagefault verify-qemu-matrix \
        verify-interrupts verify-timer verify-scheduler verify-preemption \
        verify-prompt3 verify-user-build verify-initramfs verify-user-mode \
        verify-syscalls verify-user-fault verify-prompt4
.DEFAULT_GOAL := all

all: bootloader kernel
bootloader: dirs $(BOOTX64)
kernel: dirs $(KERNEL)
user: dirs $(USER_PROGS)

dirs:
	@mkdir -p $(B_BOOT) $(B_KERN) $(B_IMG) $(B_USER)

# ---- Bootloader (PE/COFF EFI application) ----------------------------------
$(B_BOOT)/%.obj: bootloader/src/%.c $(BOOT_HDR) | dirs
	$(call need,$(CC),Install LLVM/clang. macOS: brew install llvm)
	$(CC) $(BOOT_CFLAGS) -c $< -o $@

$(BOOTX64): $(BOOT_OBJ)
	$(call need,$(LD_EFI),Install LLVM lld. macOS: brew install lld   (provides lld-link))
	$(LD_EFI) $(BOOT_LDFLAGS) /out:$@ $(BOOT_OBJ)
	@echo "[OK] $@"

# ---- Kernel (ELF64) --------------------------------------------------------
$(B_KERN)/%.o: %.c $(KERN_HDR) | dirs
	$(call need,$(CC),Install LLVM/clang. macOS: brew install llvm)
	$(CC) $(KERN_CFLAGS) -c $< -o $@

$(B_KERN)/%.o: %.S | dirs
	$(call need,$(CC),Install LLVM/clang. macOS: brew install llvm)
	$(CC) $(KERN_ASFLAGS) -c $< -o $@

$(KERNEL): $(KERN_OBJ)
	$(call need,$(LD_KERNEL),Install LLVM lld. macOS: brew install lld   (provides ld.lld))
	$(LD_KERNEL) $(KERN_LDFLAGS) -o $@ $(KERN_OBJ)
	@echo "[OK] $@"

# ---- User-space programs (ring 3, HXE1) ------------------------------------
$(B_USER)/%.o: user/lib/%.c $(USER_HDR) | dirs
	$(call need,$(CC),Install LLVM/clang. macOS: brew install llvm)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(B_USER)/crt0.o: user/lib/crt0.S $(USER_HDR) | dirs
	$(call need,$(CC),Install LLVM/clang. macOS: brew install llvm)
	$(CC) $(USER_ASFLAGS) -c $< -o $@

$(B_USER)/init.o: user/init/init.c $(USER_HDR) | dirs
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(B_USER)/syscall_test.o: user/tests/syscall_test.c $(USER_HDR) | dirs
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Link each program (crt0 first), then convert the ELF to HXE1.
$(B_USER)/%.elf: $(B_USER)/%.o $(USER_CRT0) $(USER_LIB_OBJ)
	$(call need,$(LD_KERNEL),Install LLVM lld. macOS: brew install lld   (provides ld.lld))
	$(LD_KERNEL) $(USER_LDFLAGS) -o $@ $(USER_CRT0) $< $(USER_LIB_OBJ)

$(B_USER)/%.hxe: $(B_USER)/%.elf
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/mkhxe.py $< $@

# ---- Initramfs (HXF1 archive) ----------------------------------------------
initramfs: user
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/mkinitramfs.py --out $(INITRAMFS) \
	  /bin/init.hxe $(B_USER)/init.hxe \
	  /bin/syscall_test.hxe $(B_USER)/syscall_test.hxe \
	  /etc/banner.txt user/init/banner.txt

# ---- Disk image (stage 6 + Prompt 4 initramfs) -----------------------------
image: all initramfs
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/build_image.py --bootloader $(BOOTX64) --kernel $(KERNEL) \
	  --initramfs $(INITRAMFS) --out $(IMAGE)

# ---- Run / debug (stage 5) -------------------------------------------------
run: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/run_qemu.py --image $(IMAGE)

debug: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/run_qemu.py --image $(IMAGE) --debug

clean:
	rm -rf $(B_BOOT)/* $(B_KERN)/* $(B_IMG)/* $(B_USER)/*
	@echo "[OK] cleaned build/"

# ---- Inspection ------------------------------------------------------------
inspect: all
	@command -v file >/dev/null 2>&1 || { echo '[DEP MISSING] file. macOS: preinstalled; Debian: apt-get install file'; exit 1; }
	@test -x "$(READELF)" || command -v "$(READELF)" >/dev/null 2>&1 || { echo '[DEP MISSING] readelf/llvm-readelf. macOS: brew install llvm'; exit 1; }
	@test -x "$(NM)" || command -v "$(NM)" >/dev/null 2>&1 || { echo '[DEP MISSING] nm/llvm-nm. macOS: brew install llvm'; exit 1; }
	@echo '=== file: BOOTX64.EFI ==='
	@file $(BOOTX64)
	@echo '=== file: kernel.elf ==='
	@file $(KERNEL)
	@echo '=== kernel ELF header ==='
	@$(READELF) -h $(KERNEL)
	@echo '=== kernel program headers ==='
	@$(READELF) -l $(KERNEL) | sed -n '1,25p'
	@echo '=== kernel symbols (entry + linker symbols) ==='
	@$(NM) $(KERNEL) | grep -E 'kernel_entry|kernel_main|__kernel_start|__kernel_end|__text_start|__text_end|__rodata_start|__rodata_end|__data_start|__data_end|__bss_start|__bss_end|kernel_stack_top' | sort

# ---- Lines of code (excludes build/) ---------------------------------------
loc:
	@printf 'bootloader : %s\n' "$$(find bootloader -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' \) -exec cat {} + | wc -l | tr -d ' ')"
	@printf 'kernel     : %s\n' "$$(find kernel -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' \) -exec cat {} + | wc -l | tr -d ' ')"
	@printf 'shared     : %s\n' "$$(find shared -type f -name '*.h' -exec cat {} + | wc -l | tr -d ' ')"
	@printf 'tools      : %s\n' "$$(find tools -type f -name '*.py' -exec cat {} + | wc -l | tr -d ' ')"
	@printf 'docs       : %s\n' "$$(find docs -type f -name '*.md' -exec cat {} + | wc -l | tr -d ' ')"
	@printf 'total      : %s\n' "$$(find bootloader kernel shared tools docs -type f \( -name '*.c' -o -name '*.h' -o -name '*.S' -o -name '*.ld' -o -name '*.py' -o -name '*.md' \) -exec cat {} + | wc -l | tr -d ' ')"

# ---- Headless run (serial on stdio, no window) -----------------------------
run-headless: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/run_qemu.py --image $(IMAGE) --headless

# ---- Boot verification (normal image) --------------------------------------
verify-boot: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name boot --timeout 60 \
	  --log $(B_IMG)/serial.log \
	  --expect "MyOS Kernel 0.0.4" \
	  --expect "[OK] Kernel entered" \
	  --expect "[OK] GDT loaded" \
	  --expect "[OK] TSS loaded" \
	  --expect "[OK] IDT loaded" \
	  --expect "[OK] Exceptions online" \
	  --expect "[OK] Physical memory manager online" \
	  --expect "[OK] Kernel CR3 loaded" \
	  --expect "[OK] Kernel heap online" \
	  --expect "[OK] Early kernel tests passed"

# ---- Exception verification (separate destructive build) -------------------
verify-exception:
	$(call need,$(PY),Install Python 3.)
	$(MAKE) clean
	$(MAKE) image EXTRA_KCFLAGS="-DMYOS_TEST_INVALID_OPCODE=1"
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name exception --timeout 60 \
	  --log $(B_IMG)/exception.log \
	  --expect "CPU EXCEPTION" \
	  --expect "vector     : 0x0000000000000006" \
	  --expect "#UD Invalid Opcode"
	$(MAKE) clean
	$(MAKE) image
	@echo "[OK] verify-exception complete (normal image restored)"

# ---- Page-fault verification (separate destructive build) ------------------
verify-pagefault:
	$(call need,$(PY),Install Python 3.)
	$(MAKE) clean
	$(MAKE) image EXTRA_KCFLAGS="-DMYOS_TEST_PAGE_FAULT=1"
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name pagefault --timeout 60 \
	  --log $(B_IMG)/pagefault.log \
	  --expect "CPU EXCEPTION" \
	  --expect "#PF Page Fault" \
	  --expect "fault addr" \
	  --expect "error code"
	$(MAKE) clean
	$(MAKE) image
	@echo "[OK] verify-pagefault complete (normal image restored)"

# ---- Prompt 3 verification --------------------------------------------------
verify-interrupts: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name interrupts --timeout 60 \
	  --log $(B_IMG)/interrupts.log \
	  --expect "[OK] Legacy PIC disabled" \
	  --expect "[OK] ACPI MADT parsed" \
	  --expect "[OK] Local APIC discovered" \
	  --expect "[OK] Local APIC enabled" \
	  --expect "[OK] IRQ dispatcher online"

verify-timer: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name timer --timeout 60 \
	  --log $(B_IMG)/timer.log \
	  --expect "[OK] PIT timer online" \
	  --expect "[OK] Local APIC timer online" \
	  --expect "[OK] Kernel tick online" \
	  --expect "[OK] Kernel ticks increasing"

verify-scheduler: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name scheduler --timeout 60 \
	  --log $(B_IMG)/scheduler.log \
	  --expect "[OK] Context switch online" \
	  --expect "[OK] Scheduler online" \
	  --expect "[OK] Sleep/wakeup online" \
	  --expect "[PASS] scheduler round-robin" \
	  --expect "[PASS] sleep/wakeup" \
	  --expect "[OK] Scheduler tests passed"

verify-preemption: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name preemption --timeout 60 \
	  --log $(B_IMG)/preemption.log \
	  --expect "[OK] Timer preemption online" \
	  --expect "[PASS] timer preemption"

verify-prompt3:
	$(MAKE) verify-boot
	$(MAKE) verify-exception
	$(MAKE) verify-pagefault
	$(MAKE) verify-interrupts
	$(MAKE) verify-timer
	$(MAKE) verify-scheduler
	$(MAKE) verify-preemption
	@echo "[OK] verify-prompt3 passed"

# ---- Prompt 4 verification --------------------------------------------------
# Build artifacts exist (no QEMU needed).
verify-user-build: user initramfs
	@test -f $(B_USER)/init.hxe         || { echo "[FAIL] verify-user-build: missing $(B_USER)/init.hxe"; exit 1; }
	@test -f $(B_USER)/syscall_test.hxe || { echo "[FAIL] verify-user-build: missing $(B_USER)/syscall_test.hxe"; exit 1; }
	@test -f $(INITRAMFS)               || { echo "[FAIL] verify-user-build: missing $(INITRAMFS)"; exit 1; }
	@echo "[PASS] verify-user-build (init.hxe, syscall_test.hxe, initramfs.hxf present)"

verify-initramfs: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name initramfs --timeout 60 \
	  --log $(B_IMG)/initramfs-run.log \
	  --expect "[OK] Initramfs file loaded" \
	  --expect "[OK] Initramfs loaded" \
	  --expect "[OK] Initramfs parsed"

verify-user-mode: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name user-mode --timeout 60 \
	  --log $(B_IMG)/user-mode.log \
	  --expect "[OK] Ring 3 entry online" \
	  --expect "[USER] hello from ring 3" \
	  --expect "[OK] First user program exited cleanly"

verify-syscalls: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name syscalls --timeout 60 \
	  --log $(B_IMG)/syscalls.log \
	  --expect "[USER] write syscall OK" \
	  --expect "[USER] getpid syscall OK" \
	  --expect "[USER] yield syscall OK" \
	  --expect "[USER] sleep syscall OK" \
	  --expect "[USER] read syscall OK" \
	  --expect "[USER] exit syscall OK" \
	  --expect "[OK] User/kernel boundary tests passed"

# Destructive: build a kernel that launches a user program which faults in ring 3.
verify-user-fault:
	$(call need,$(PY),Install Python 3.)
	$(MAKE) clean
	$(MAKE) image EXTRA_KCFLAGS="-DMYOS_TEST_USER_FAULT=1"
	$(PY) tools/verify_qemu.py --image $(IMAGE) --test-name user-fault --timeout 60 \
	  --log $(B_IMG)/user-fault.log \
	  --expect "[TEST] user fault isolation" \
	  --expect "[OK] User fault isolated" \
	  --expect "[OK] User/kernel boundary tests passed"
	$(MAKE) clean
	$(MAKE) image
	@echo "[OK] verify-user-fault complete (normal image restored)"

verify-prompt4:
	$(MAKE) verify-boot
	$(MAKE) verify-exception
	$(MAKE) verify-pagefault
	$(MAKE) verify-interrupts
	$(MAKE) verify-timer
	$(MAKE) verify-scheduler
	$(MAKE) verify-preemption
	$(MAKE) verify-user-build
	$(MAKE) verify-initramfs
	$(MAKE) verify-user-mode
	$(MAKE) verify-syscalls
	$(MAKE) verify-user-fault
	@echo "[OK] verify-prompt4 passed"

# ---- Memory-size matrix ----------------------------------------------------
verify-qemu-matrix: image
	$(call need,$(PY),Install Python 3.)
	@for m in 128M 256M 512M 1024M 2048M; do \
	  $(PY) tools/verify_qemu.py --image $(IMAGE) --mem $$m --test-name matrix-$$m \
	    --timeout 60 --log $(B_IMG)/matrix-$$m.log \
	    --expect "[OK] Early kernel tests passed" || exit 1; \
	done
	@echo "[OK] verify-qemu-matrix passed (128M 256M 512M 1024M 2048M)"
