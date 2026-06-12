# MyOS — x86-64 UEFI boot pipeline (stages 1-16) + kernel foundation (Prompt 2)
#
# Targets: all bootloader kernel image run debug clean

# ---- Toolchain -------------------------------------------------------------
CC        := clang
LD_EFI    := lld-link
LD_KERNEL := ld.lld
PY        := python3

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
KERN_CFLAGS := --target=x86_64-unknown-none-elf -ffreestanding -fno-stack-protector \
               -fno-builtin -fno-pic -fno-pie -mno-red-zone -mcmodel=kernel \
               -nostdlib -Wall -Wextra $(KERN_INCLUDES)
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
.PHONY: all bootloader kernel image run debug clean dirs
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
