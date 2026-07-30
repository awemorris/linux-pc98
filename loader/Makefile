AS := as
LD := ld
OBJCOPY := objcopy
ASFLAGS := --32
all: boot2.bin disk-ipl.bin partition-pbr.bin fat-loader.bin
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
clean:
	rm -f *.o *.elf *.bin
.PHONY: all clean
