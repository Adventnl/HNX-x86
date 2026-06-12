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

# ---- Flags -----------------------------------------------------------------
BOOT_CFLAGS := --target=x86_64-unknown-windows -ffreestanding -fshort-wchar \
               -mno-red-zone -fno-stack-protector -fno-builtin -Wall -Wextra \
               -Ibootloader/include -Ishared/include
BOOT_LDFLAGS := /subsystem:efi_application /entry:EfiMain /nodefaultlib

KERN_INCLUDES := -Ikernel/include -Ikernel/arch/x86_64 -Ikernel/memory \
                 -Ikernel/tests -Ishared/include
# EXTRA_KCFLAGS is injected by the verify-* targets to enable destructive tests.
EXTRA_KCFLAGS ?=
KERN_CFLAGS := --target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
               -fno-builtin -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
               -nostdlib -Wall -Wextra $(KERN_INCLUDES) $(EXTRA_KCFLAGS)
KERN_ASFLAGS := --target=x86_64-unknown-none-elf $(KERN_INCLUDES)
KERN_LDFLAGS := -T kernel/arch/x86_64/linker.ld

# ---- Sources / objects -----------------------------------------------------
BOOT_SRC := $(wildcard bootloader/src/*.c)
BOOT_OBJ := $(patsubst bootloader/src/%.c,$(B_BOOT)/%.obj,$(BOOT_SRC))
BOOT_HDR := $(wildcard bootloader/include/*.h) shared/include/boot_info.h

# Kernel C/asm sources span src/, arch/x86_64/, memory/, tests/. Object file
# names are flattened into build/kernel/ (basenames are unique across dirs).
KERN_CSRC := $(wildcard kernel/src/*.c) $(wildcard kernel/arch/x86_64/*.c) \
             $(wildcard kernel/memory/*.c) $(wildcard kernel/tests/*.c)
KERN_SSRC := $(wildcard kernel/arch/x86_64/*.S)
KERN_OBJ  := $(addprefix $(B_KERN)/,$(notdir $(KERN_CSRC:.c=.o))) \
             $(addprefix $(B_KERN)/,$(notdir $(KERN_SSRC:.S=.o)))
KERN_HDR  := $(wildcard kernel/include/*.h) $(wildcard kernel/arch/x86_64/*.h) \
             $(wildcard kernel/memory/*.h) $(wildcard kernel/tests/*.h) \
             shared/include/boot_info.h

vpath %.c kernel/src kernel/arch/x86_64 kernel/memory kernel/tests
vpath %.S kernel/arch/x86_64

BOOTX64 := $(B_BOOT)/BOOTX64.EFI
KERNEL  := $(B_KERN)/kernel.elf
IMAGE   := $(B_IMG)/myos.img

# ---- Tool-presence guard ---------------------------------------------------
# Usage: $(call need,<tool>,<install hint without commas>)
define need
@command -v $(1) >/dev/null 2>&1 || { printf '\n[DEP MISSING] required tool "%s" not found on PATH.\n  %s\n\n' "$(1)" "$(2)"; exit 1; }
endef

# ---- Phony targets ---------------------------------------------------------
.PHONY: all bootloader kernel image run debug clean dirs \
        inspect loc run-headless \
        verify-boot verify-exception verify-pagefault verify-qemu-matrix
.DEFAULT_GOAL := all

all: bootloader kernel
bootloader: dirs $(BOOTX64)
kernel: dirs $(KERNEL)

dirs:
	@mkdir -p $(B_BOOT) $(B_KERN) $(B_IMG)

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

# ---- Disk image (stage 6) --------------------------------------------------
image: all
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/build_image.py --bootloader $(BOOTX64) --kernel $(KERNEL) --out $(IMAGE)

# ---- Run / debug (stage 5) -------------------------------------------------
run: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/run_qemu.py --image $(IMAGE)

debug: image
	$(call need,$(PY),Install Python 3.)
	$(PY) tools/run_qemu.py --image $(IMAGE) --debug

clean:
	rm -rf $(B_BOOT)/* $(B_KERN)/* $(B_IMG)/*
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
	  --expect "MyOS Kernel 0.0.2" \
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

# ---- Memory-size matrix ----------------------------------------------------
verify-qemu-matrix: image
	$(call need,$(PY),Install Python 3.)
	@for m in 128M 256M 512M 1024M 2048M; do \
	  $(PY) tools/verify_qemu.py --image $(IMAGE) --mem $$m --test-name matrix-$$m \
	    --timeout 60 --log $(B_IMG)/matrix-$$m.log \
	    --expect "[OK] Early kernel tests passed" || exit 1; \
	done
	@echo "[OK] verify-qemu-matrix passed (128M 256M 512M 1024M 2048M)"
