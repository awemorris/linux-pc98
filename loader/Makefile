AS := as
LD := ld
OBJCOPY := objcopy
CC := gcc
ASFLAGS := --32
STAGE2_CFLAGS := -m32 -march=i386 -Os -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror

all: boot2.bin disk-ipl.bin partition-pbr.bin fat-loader.bin boot98-stage1.bin boot98-chain-test.bin boot98-fdd-ipl.bin BOOT98.BIN boot98-iplware-test.bin boot98-iplware-com-test.com BOOTAPP.BIN

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

disk-ipl.bin: disk-ipl.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 512

partition-pbr.o: partition-pbr.S
	$(AS) $(ASFLAGS) $< -o $@

partition-pbr.elf: partition-pbr.o
	$(LD) -m elf_i386 -Ttext=0 -e _start $< -o $@

partition-pbr.bin: partition-pbr.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@test $$(stat -c%s $@) -eq 512

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

boot98-stage1.bin: boot98-stage1.elf
	$(OBJCOPY) -O binary -j .text $< $@
	@sz=$$(stat -c%s $@); remaining=$$((7168 - sz)); echo "boot98-stage1.bin: $$sz bytes ($$remaining bytes free of 7 KiB)"; test $$sz -le 7168

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

boot98-stage2.o: boot98-stage2.c boot98-abi.h
	$(CC) $(STAGE2_CFLAGS) -c $< -o $@

boot98-stage2.elf: boot98-stage2-entry.o boot98-stage2.o boot98-stage2.ld
	$(LD) -m elf_i386 -T boot98-stage2.ld -nostdlib boot98-stage2-entry.o boot98-stage2.o -o $@

BOOT98.BIN: boot98-stage2.elf patch-boot98-bin.py
	$(OBJCOPY) -O binary $< $@
	python3 patch-boot98-bin.py $@

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

BOOTAPP.BIN: boot98-applet-test.elf patch-boot98-applet.py
	$(OBJCOPY) -O binary $< $@
	python3 patch-boot98-applet.py $@

clean:
	rm -f *.o *.elf *.bin *.com *.BIN

.PHONY: all clean
