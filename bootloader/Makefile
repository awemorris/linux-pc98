AS := as
LD := ld
OBJCOPY := objcopy
CC := gcc
HOSTCC ?= cc
.DEFAULT_GOAL := all
ASFLAGS := --32
STAGE2_CFLAGS := -m32 -march=i386 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror
SCRIPTS_DIR := ../scripts

include libc/libc.mk
include softfloat/softfloat.mk
include noct.mk

# Milestone verification nests QEMU tests.  Keep those chains ordered even
# when the caller requests a highly parallel compile.
.NOTPARALLEL: noct-m9-verify noct-m10-verify noct-m11-verify \
	noct-m14-verify noct-m15-verify noct-m17-verify

STAGE2_OBJS := boot98-stage2-entry.o boot98-stage2.o boot98-console.o \
	boot98-env.o boot98-fs.o boot98-beui.o \
	boot98-fat.o boot98-fat16.o boot98-image.o \
	boot98-noct.o boot98-noct-memory.o boot98-noct-napi.o \
	boot98-noct-platform.o boot98-noct-target.o \
	$(NOCT_OBJECTS) $(BOOT98_LIBC_OBJECTS) $(BOOT98_SOFTFLOAT_OBJECTS)
M9_STAGE2_OBJS := $(filter-out boot98-stage2.o,$(STAGE2_OBJS)) \
	boot98-stage2-m9-test.o

all: boot2.bin ipl-lba0.bin ipl-lba2.bin ipl-lba0.img ipl-lba2.img \
	ipl-part.img IO.SYS BOOT.SYS \
	partition-pbr.bin fat-loader.bin boot98-chain-test.bin \
	boot98-fdd-ipl.bin boot98-iplware-test.bin \
	boot98-iplware-com-test.com BOOTAPP.BIN

boot2.o: boot2.S
	$(AS) $(ASFLAGS) $< -o $@

boot2.elf: boot2.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

boot2.bin: boot2.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@sz=$$(stat -c%s $@); echo "boot2.bin: $$sz bytes"; if [ $$sz -gt 1024 ]; then echo "ERROR: IPL > 1024"; exit 1; fi

disk-ipl.o: disk-ipl.S
	$(AS) $(ASFLAGS) $< -o $@

disk-ipl.elf: disk-ipl.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

ipl-lba0.bin: disk-ipl.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 512

ipl-lba0.img: ipl-lba0.bin
	cp $< $@

# Compatibility target for older local scripts.  Release artifacts use the
# descriptive on-disk name above.
disk-ipl.bin: ipl-lba0.bin
	cp $< $@

boot98-lba2.o: boot98-lba2.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-lba2.elf: boot98-lba2.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

ipl-lba2.bin: boot98-lba2.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 7168

ipl-lba2.img: ipl-lba2.bin
	cp $< $@

partition-pbr.o: partition-pbr.S
	$(AS) $(ASFLAGS) $< -o $@

partition-pbr.elf: partition-pbr.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

partition-pbr.bin: partition-pbr.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 1024

ipl-part.img: partition-pbr.bin
	cp $< $@

fat-loader.o: fat-loader.S
	$(AS) $(ASFLAGS) $< -o $@

fat-loader.elf: fat-loader.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

fat-loader.bin: fat-loader.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@sz=$$(stat -c%s $@); echo "fat-loader.bin: $$sz bytes"; if [ $$sz -gt 57344 ]; then echo "ERROR: loader > 56 KiB"; exit 1; fi

boot98-stage1.o: boot98-stage1.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-stage1.elf: boot98-stage1.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

IO.SYS: boot98-stage1.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@sz=$$(stat -c%s $@); echo "IO.SYS: $$sz bytes"; test $$sz -le 65024

boot98-stage1.bin: IO.SYS
	cp $< $@

boot98-chain-test.o: boot98-chain-test.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-chain-test.elf: boot98-chain-test.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

boot98-chain-test.bin: boot98-chain-test.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 512

boot98-fdd-ipl.o: boot98-fdd-ipl.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-fdd-ipl.elf: boot98-fdd-ipl.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

boot98-fdd-ipl.bin: boot98-fdd-ipl.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 512

boot98-stage2-entry.o: boot98-stage2-entry.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-messages.h: boot98-messages.txt \
	$(SCRIPTS_DIR)/generate-boot98-messages.py
	python3 $(SCRIPTS_DIR)/generate-boot98-messages.py $< $@

boot98-stage2.o: boot98-stage2.c boot98-abi.h boot98-console.h \
	boot98-messages.h boot98-fat16.h boot98-fs.h boot98-image.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-stage2-m9-test.o: boot98-stage2.c boot98-abi.h boot98-console.h \
	boot98-messages.h boot98-fat16.h boot98-fs.h boot98-image.h
	$(CC) $(STAGE2_CFLAGS) -DBOOT98_M9_WRITE_TEST -c $< -o $@

boot98-console.o: boot98-console.c boot98-console.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-env.o: boot98-env.c boot98-env.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-beui.o: boot98-beui.c boot98-beui.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-fs.o: boot98-fs.c boot98-fs.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-fat.o: boot98-fat.c boot98-fat.h boot98-fs.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-fat16.o: boot98-fat16.c boot98-fat16.h boot98-fat.h boot98-fs.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-image.o: boot98-image.c boot98-image.h boot98-fs.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-noct.o: boot98-noct.c boot98-noct.h boot98-noct-napi.h \
	boot98-noct-memory.h libc/boot98-heap.h \
	$(NOCT_ROOT)/include/noct/repl.h
	$(CC) $(NOCT_CPPFLAGS) $(NOCT_CFLAGS) -c $< -o $@

boot98-noct-memory.o: boot98-noct-memory.c boot98-noct-memory.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-noct-napi.o: boot98-noct-napi.c boot98-noct-napi.h boot98-noct.h \
	boot98-noct-memory.h boot98-env.h boot98-beui.h libc/boot98-heap.h
	$(CC) $(NOCT_CPPFLAGS) $(NOCT_CFLAGS) -c $< -o $@

boot98-noct-target.o: boot98-noct-target.c boot98-noct-target.h \
	boot98-noct-napi.h $(NOCT_ROOT)/include/noct/noct.h
	$(CC) $(NOCT_CPPFLAGS) $(NOCT_CFLAGS) -c $< -o $@

boot98-noct-platform.o: boot98-noct-platform.c boot98-noct-platform.h \
	boot98-noct.h boot98-noct-memory.h boot98-noct-napi.h \
	boot98-noct-m6-script.h \
	boot98-console.h boot98-env.h boot98-fs.h
	$(CC) $(NOCT_CPPFLAGS) $(BOOT98_LIBC_CPPFLAGS) -I. \
		$(BOOT98_LIBC_CFLAGS) -c $< -o $@

boot98-stage2.elf: $(STAGE2_OBJS) boot98-stage2.ld
	$(LD) -m elf_i386 -T boot98-stage2.ld -nostdlib $(STAGE2_OBJS) -o $@

BOOT.SYS: boot98-stage2.elf $(SCRIPTS_DIR)/patch-boot98-bin.py
	$(OBJCOPY) -O binary $< $@
	python3 $(SCRIPTS_DIR)/patch-boot98-bin.py $@

boot98-stage2-m9-test.elf: $(M9_STAGE2_OBJS) boot98-stage2.ld
	$(LD) -m elf_i386 -T boot98-stage2.ld -nostdlib $(M9_STAGE2_OBJS) -o $@

BOOT-M9.SYS: boot98-stage2-m9-test.elf $(SCRIPTS_DIR)/patch-boot98-bin.py
	$(OBJCOPY) -O binary $< $@
	python3 $(SCRIPTS_DIR)/patch-boot98-bin.py $@

boot98-iplware-test.o: boot98-iplware-test.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-iplware-test.elf: boot98-iplware-test.o
	$(LD) -m elf_i386 -Ttext=0x100 -e _start $< -o $@

boot98-iplware-test.bin: boot98-iplware-test.elf
	$(OBJCOPY) -O binary -j .text $< $@

boot98-iplware-com-test.o: boot98-iplware-com-test.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-iplware-com-test.elf: boot98-iplware-com-test.o
	$(LD) -m elf_i386 -Ttext=0x100 -e _start $< -o $@

boot98-iplware-com-test.com: boot98-iplware-com-test.elf
	$(OBJCOPY) -O binary -j .text $< $@

boot98-applet-test.o: boot98-applet-test.S
	$(AS) $(ASFLAGS) $< -o $@

boot98-applet-test.elf: boot98-applet-test.o boot98-applet.ld
	$(LD) -m elf_i386 -T boot98-applet.ld -nostdlib $< -o $@

BOOTAPP.BIN: boot98-applet-test.elf $(SCRIPTS_DIR)/patch-boot98-applet.py
	$(OBJCOPY) -O binary $< $@
	python3 $(SCRIPTS_DIR)/patch-boot98-applet.py $@

tests/boot98-fat-host-test: tests/boot98-fat-host-test.c boot98-fs.c \
	boot98-fat.c boot98-fat16.c boot98-fs.h boot98-fat.h boot98-fat16.h
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -I. \
		boot98-fs.c boot98-fat.c boot98-fat16.c $< -o $@

tests/boot98-fat-write-host-test: tests/boot98-fat-write-host-test.c \
	boot98-fs.c boot98-fat.c boot98-fat16.c boot98-fs.h boot98-fat.h \
	boot98-fat16.h
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -I. \
		boot98-fs.c boot98-fat.c boot98-fat16.c $< -o $@

BOOT98_STDIO_FS_TEST := ../build/bootloader/tests/boot98-stdio-fs-host-test

$(BOOT98_STDIO_FS_TEST): tests/boot98-stdio-fs-host-test.c boot98-fs.c \
	boot98-fs.h $(BOOT98_LIBC_SOURCE_REL)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(BOOT98_HOST_TEST_CFLAGS) -I. boot98-fs.c \
		$(BOOT98_LIBC_SOURCE_REL) $< -o $@

boot98-stdio-fs-host-test: $(BOOT98_STDIO_FS_TEST)
	$(BOOT98_STDIO_FS_TEST)

BOOT98_NOCT_TEST := ../build/bootloader/tests/boot98-noct-host-test

tests/boot98-env-host-test: tests/boot98-env-host-test.c boot98-env.c \
	boot98-env.h
	$(HOSTCC) -Wall -Wextra -Werror -I. boot98-env.c $< -o $@

tests/boot98-noct-memory-host-test: tests/boot98-noct-memory-host-test.c \
	boot98-noct-memory.c boot98-noct-memory.h
	$(HOSTCC) -Wall -Wextra -Werror -I. boot98-noct-memory.c $< -o $@

tests/boot98-beui-host-test: tests/boot98-beui-host-test.c \
	boot98-beui.c boot98-beui.h
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -I. \
		boot98-beui.c $< -o $@

$(BOOT98_NOCT_TEST): tests/boot98-noct-host-test.c \
	LS.NCT CP.NCT \
	boot98-noct-m6-script.h boot98-noct.o boot98-noct-napi.o \
	boot98-noct-target.o boot98-beui.o \
	boot98-env.o boot98-fs.o $(NOCT_OBJECTS) $(BOOT98_LIBC_OBJECTS) \
	$(BOOT98_SOFTFLOAT_OBJECTS)
	@mkdir -p $(dir $@)
	$(HOSTCC) -m32 -no-pie -fno-builtin -fno-stack-protector -Wall -Wextra \
		-Werror -I. -Ilibc/include -Ilibc -I$(NOCT_ROOT)/include \
		-DBOOT98_NOCT_JIT_CODE_MAX=$(NOCT_JIT_CODE_MAX) \
	tests/boot98-noct-host-test.c boot98-noct.o boot98-noct-napi.o \
		boot98-noct-target.o boot98-beui.o \
		boot98-env.o boot98-fs.o $(NOCT_OBJECTS) $(BOOT98_LIBC_OBJECTS) \
		$(BOOT98_SOFTFLOAT_OBJECTS) -o $@

NOCT_M6_JIT_CODE := ../build/logs/boot98-m6-jit-code.bin
NOCT_TEST_JIT_CODE_SIZE := 98304

boot98-noct-host-test: $(BOOT98_NOCT_TEST)
	@mkdir -p $(dir $(NOCT_M6_JIT_CODE))
	$(BOOT98_NOCT_TEST) $(NOCT_M6_JIT_CODE) LS.NCT CP.NCT
	@test $$(stat -c%s $(NOCT_M6_JIT_CODE)) -eq $(NOCT_TEST_JIT_CODE_SIZE)
	@echo "BOOT98 Noct interpreter/JIT lifecycle host tests: PASS"

noct-m4-opcode-check: boot98-noct.o boot98-noct-platform.o
	@if $(NOCT_OBJDUMP) -d --no-show-raw-insn $^ | \
		grep -E '(^[[:space:]]*[0-9a-f]+:[[:space:]]+f[a-z0-9]+[[:space:]])|\b(bswap|cmpxchg|xadd|cmov[a-z]*|rdtsc|ud2|cpuid|fx[a-z]+|movaps|movups|xmm[0-9]|ymm[0-9]|zmm[0-9])\b'; then \
		echo "ERROR: M4 glue contains a post-i386 opcode" >&2; \
		exit 1; \
	fi
	@echo "BOOT98 Noct M4 glue i386 opcode check: PASS"

noct-m4-verify: noct-m3-verify boot98-noct-host-test \
	noct-m4-opcode-check BOOT.SYS
	@echo "PC98BE M4 historical lifecycle checks: PASS"

NOCT_M5_DISASSEMBLY := ../build/logs/boot98-m5.disassembly
NOCT_M5_REJECTED := ../build/logs/boot98-m5-rejected.txt

noct-m5-final-opcode-check: boot98-stage2.elf boot98-softfloat-opcode-check
	@mkdir -p $(dir $(NOCT_M5_DISASSEMBLY))
	@$(NOCT_OBJDUMP) -d --no-show-raw-insn boot98-stage2.elf > \
		$(NOCT_M5_DISASSEMBLY)
	@grep -E '(^[[:space:]]*[0-9a-f]+:[[:space:]]+f[a-z0-9]+[[:space:]])|\b(bswap|cmpxchg|xadd|cmov[a-z]*|rdtsc|ud2|cpuid|fx[a-z]+|movaps|movups|xmm[0-9]|ymm[0-9]|zmm[0-9])\b' \
		$(NOCT_M5_DISASSEMBLY) > $(NOCT_M5_REJECTED) || true
	@if test -s $(NOCT_M5_REJECTED); then \
		echo "ERROR: final BOOT.SYS ELF contains a forbidden opcode" >&2; \
		cat $(NOCT_M5_REJECTED) >&2; \
		exit 1; \
	fi
	@echo "BOOT98 M5 final i386 opcode check: PASS"

noct-m5-verify: noct-m4-verify boot98-softfloat-host-test \
	noct-m5-final-opcode-check
	@echo "PC98BE M5 historical soft-float checks: PASS"

noct-m6-verify: noct-m5-verify boot98-noct-host-test BOOT.SYS
	@echo "PC98BE M6 verification: PASS (forced i386 JIT)"

noct-m7-verify: noct-m6-verify boot98-noct-host-test BOOT.SYS
	@echo "PC98BE M7 host/build verification: PASS (arguments and main signature)"

noct-m8-verify: noct-m7-verify boot98-noct-host-test BOOT.SYS \
	noct-m5-final-opcode-check
	@echo "PC98BE M8 host/build verification: PASS (safe native APIs)"

boot98-bios-write-qemu-test: BOOT-M9.SYS
	$(SCRIPTS_DIR)/test-boot98-bios-write.sh all

noct-m9-verify: noct-m8-verify check BOOT.SYS BOOT-M9.SYS \
	boot98-bios-write-qemu-test
	@echo "PC98BE M9 verification: PASS (IDE/SCSI BIOS write/read/restore)"

boot98-noct-file-qemu-test: BOOT.SYS
	$(SCRIPTS_DIR)/test-boot98-noct-file.sh

noct-m10-verify: noct-m9-verify check BOOT.SYS \
	noct-m5-final-opcode-check boot98-noct-file-qemu-test
	@echo "PC98BE M10 verification: PASS (FAT16 writer and Noct File API)"

boot98-noct-utilities-qemu-test: BOOT.SYS LS.NCT CP.NCT
	$(SCRIPTS_DIR)/test-boot98-noct-utilities.sh

noct-m11-verify: noct-m10-verify check BOOT.SYS \
	noct-m5-final-opcode-check boot98-noct-utilities-qemu-test
	@echo "PC98BE M11 safe utilities verification: PASS (LS.NCT and CP.NCT)"

boot98-noct-env-qemu-test: BOOT.SYS
	$(SCRIPTS_DIR)/test-boot98-noct-env.sh

noct-m14-verify: noct-m11-verify check BOOT.SYS \
	noct-m5-final-opcode-check boot98-noct-env-qemu-test
	@echo "Boots M14 verification: PASS (environment and intrinsic APIs)"

boot98-noct-repl-qemu-test: BOOT.SYS
	$(SCRIPTS_DIR)/test-boot98-noct-repl.sh

noct-m15-verify: noct-m14-verify check BOOT.SYS \
	noct-m5-final-opcode-check boot98-noct-repl-qemu-test
	@echo "Boots M15 REPL verification: PASS (keyboard/error/Ctrl-C on i386)"

noct-m17-verify: noct-m15-verify check BOOT.SYS \
	noct-m5-final-opcode-check
	@for memory in 5 8 16 32 64 96; do \
		echo "Testing Boots Noct RAM profile: $${memory} MiB"; \
		BOOT98_TEST_MEMORY_MIB=$$memory \
			$(SCRIPTS_DIR)/test-boot98-noct-repl.sh || exit $$?; \
	done
	@echo "Boots M17 verification: PASS (5/8/16/32/64/>64 MiB profiles)"

beui-g1-verify: check BOOT.SYS noct-m5-final-opcode-check
	@echo "Boots BeUI G1 verification: PASS (lifecycle and HAL boundary)"

check: tests/boot98-fat-host-test tests/boot98-fat-write-host-test \
	tests/boot98-env-host-test tests/boot98-noct-memory-host-test \
	tests/boot98-beui-host-test \
	boot98-stdio-fs-host-test \
	boot98-libc-host-test \
	boot98-softfloat-host-test boot98-noct-host-test
	./tests/boot98-fat-host-test
	./tests/boot98-fat-write-host-test
	./tests/boot98-env-host-test
	./tests/boot98-noct-memory-host-test
	./tests/boot98-beui-host-test

clean: noct-clean boot98-libc-clean boot98-softfloat-clean
	rm -f *.o *.elf *.bin *.com *.BIN *.SYS tests/boot98-fat-host-test \
	tests/boot98-fat-write-host-test tests/boot98-env-host-test \
		tests/boot98-noct-memory-host-test tests/boot98-beui-host-test \
		$(BOOT98_STDIO_FS_TEST) \
		$(BOOT98_NOCT_TEST) $(NOCT_M5_DISASSEMBLY) $(NOCT_M5_REJECTED) \
		$(NOCT_M6_JIT_CODE)

.PHONY: all check clean boot98-stdio-fs-host-test boot98-noct-host-test \
	noct-m4-opcode-check \
	noct-m4-verify noct-m5-final-opcode-check noct-m5-verify

.PHONY: noct-m6-verify noct-m7-verify \
	noct-m8-verify boot98-bios-write-qemu-test noct-m9-verify

.PHONY: boot98-noct-file-qemu-test noct-m10-verify \
	boot98-noct-utilities-qemu-test noct-m11-verify \
	boot98-noct-env-qemu-test noct-m14-verify \
	boot98-noct-repl-qemu-test noct-m15-verify

.PHONY: beui-g1-verify
