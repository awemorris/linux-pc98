SRC_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OUT ?= $(abspath build/bootsimple/default)
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy

ASFLAGS := -m32 -x assembler-with-cpp -Wa,--fatal-warnings \
	-I$(SRC_DIR)/include
LDFLAGS := -m elf_i386 -Ttext=0 -e _start

.PHONY: all clean check
all: check

$(OUT):
	mkdir -p $@

$(OUT)/disk-ipl.o: $(SRC_DIR)/pc98/disk-ipl.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/lba2.o: $(SRC_DIR)/pc98/lba2.S | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/partition-pbr.o: $(SRC_DIR)/pc98/partition-pbr.S \
	$(SRC_DIR)/include/bootstrap.inc | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/io-sys.o: $(SRC_DIR)/pc98/io-sys.S $(OUT)/cmdline.bin \
	$(SRC_DIR)/include/bootstrap.inc $(SRC_DIR)/include/linux-boot.inc | $(OUT)
	cd $(OUT) && $(CC) $(ASFLAGS) -c $(abspath $<) -o io-sys.o

define link_flat
$(OUT)/$(1).elf: $(OUT)/$(2).o
	$(LD) $(LDFLAGS) -Map=$(OUT)/$(1).map $$< -o $$@
	$(OBJCOPY) -O binary -j .text $$@ $(OUT)/$(3)
endef

$(eval $(call link_flat,disk-ipl,disk-ipl,ipl-lba0.bin))
$(eval $(call link_flat,lba2,lba2,ipl-lba2.bin))
$(eval $(call link_flat,partition-pbr,partition-pbr,partition-pbr.bin))
$(eval $(call link_flat,io-sys,io-sys,IO.SYS))

check: $(OUT)/disk-ipl.elf $(OUT)/lba2.elf $(OUT)/partition-pbr.elf \
	$(OUT)/io-sys.elf
	@test "$$(stat -c %s $(OUT)/ipl-lba0.bin)" -eq 512
	@test "$$(stat -c %s $(OUT)/ipl-lba2.bin)" -eq 7168
	@test "$$(stat -c %s $(OUT)/partition-pbr.bin)" -eq 1024
	@size="$$(stat -c %s $(OUT)/IO.SYS)"; \
		test "$$size" -gt 0 && test "$$size" -le 65024 || { \
			echo "IO.SYS size $$size exceeds the PBR limit" >&2; exit 1; }
	@python3 -c 'from pathlib import Path; p=Path("$(OUT)/ipl-lba0.bin").read_bytes(); assert p[4:8] == b"IPL1" and p[510:512] == b"\x55\xaa"'
	@python3 -c 'from pathlib import Path; p=Path("$(OUT)/partition-pbr.bin").read_bytes(); assert p[510:512] == b"\x55\xaa" and p[1022:1024] == b"\x55\xaa"'
	@printf 'bootsimple: LBA0=%s LBA2=%s PBR=%s IO.SYS=%s bytes\n' \
		"$$(stat -c %s $(OUT)/ipl-lba0.bin)" \
		"$$(stat -c %s $(OUT)/ipl-lba2.bin)" \
		"$$(stat -c %s $(OUT)/partition-pbr.bin)" \
		"$$(stat -c %s $(OUT)/IO.SYS)"

clean:
	case "$(OUT)" in */build/bootsimple/*) rm -rf -- "$(OUT)" ;; \
		*) echo "Refusing unsafe OUT: $(OUT)" >&2; exit 1 ;; esac
