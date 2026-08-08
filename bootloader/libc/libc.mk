# Freestanding libc subset for the PC-98 Bootstrap Environment.
#
# M3 builds and tests this code independently.  It is deliberately not linked
# into BOOT.SYS until the later integration milestone.

BOOT98_LIBC_BUILD_DIR ?= ../build/bootloader/libc
BOOT98_TEST_BUILD_DIR ?= ../build/bootloader/tests
BOOT98_LIBC_CC ?= $(CC)
BOOT98_LIBC_LD ?= $(LD)
BOOT98_LIBC_NM ?= nm
BOOT98_LIBC_OBJDUMP ?= objdump

BOOT98_LIBC_SOURCE_REL := \
	libc/boot98-heap.c \
	libc/boot98-string.c \
	libc/boot98-ctype.c \
	libc/boot98-int64.c \
	libc/boot98-strto.c \
	libc/boot98-format.c \
	libc/boot98-stdio.c \
	libc/boot98-stdio-fs.c

BOOT98_LIBC_OBJECTS := $(patsubst libc/%.c,$(BOOT98_LIBC_BUILD_DIR)/%.o,\
	$(BOOT98_LIBC_SOURCE_REL))
BOOT98_LIBC_TEST := $(BOOT98_TEST_BUILD_DIR)/boot98-libc-host-test

BOOT98_LIBC_CPPFLAGS := -nostdinc -Ilibc/include -Ilibc
BOOT98_LIBC_CFLAGS := \
	-m32 -march=i386 -Os -ffreestanding -fno-builtin \
	-fno-pic -fno-pie -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-isolate-erroneous-paths-dereference -fno-strict-aliasing \
	-msoft-float -mno-80387 -mno-fp-ret-in-387 \
	-mno-mmx -mno-sse -mno-sse2 \
	-Wall -Wextra -Werror

BOOT98_HOST_TEST_CFLAGS := \
	-m32 -O2 -fno-builtin -fno-stack-protector \
	-Wall -Wextra -Werror \
	-Ilibc/include -Ilibc

$(BOOT98_LIBC_BUILD_DIR)/%.o: libc/%.c
	@mkdir -p $(BOOT98_LIBC_BUILD_DIR)
	$(BOOT98_LIBC_CC) $(BOOT98_LIBC_CPPFLAGS) $(BOOT98_LIBC_CFLAGS) \
		-c $< -o $@

$(BOOT98_LIBC_TEST): tests/boot98-libc-host-test.c $(BOOT98_LIBC_SOURCE_REL) \
	boot98-fs.c boot98-fs.h boot98-env.c boot98-env.h
	@mkdir -p $(BOOT98_TEST_BUILD_DIR)
	$(HOSTCC) $(BOOT98_HOST_TEST_CFLAGS) \
		boot98-fs.c boot98-env.c $(BOOT98_LIBC_SOURCE_REL) $< -o $@

boot98-libc-objects: $(BOOT98_LIBC_OBJECTS)

boot98-libc-host-test: $(BOOT98_LIBC_TEST)
	$(BOOT98_LIBC_TEST)
	@echo "BOOT98 libc host tests: PASS"

boot98-libc-opcode-check: boot98-libc-objects
	@if $(BOOT98_LIBC_OBJDUMP) -d --no-show-raw-insn $(BOOT98_LIBC_OBJECTS) | \
		grep -E '(^[[:space:]]*[0-9a-f]+:[[:space:]]+f[a-z0-9]+[[:space:]])|\b(bswap|cmpxchg|xadd|cmov[a-z]*|rdtsc|ud2|cpuid|fx[a-z]+|movaps|movups|xmm[0-9]|ymm[0-9]|zmm[0-9])\b'; then \
		echo "ERROR: BOOT98 libc objects contain a post-i386 opcode" >&2; \
		exit 1; \
	fi
	@echo "BOOT98 libc i386 opcode check: PASS"

boot98-libc-clean:
	rm -f $(BOOT98_LIBC_OBJECTS) $(BOOT98_LIBC_TEST)
	@rmdir $(BOOT98_LIBC_BUILD_DIR) $(BOOT98_TEST_BUILD_DIR) 2>/dev/null || true

.PHONY: boot98-libc-objects boot98-libc-host-test \
	boot98-libc-opcode-check boot98-libc-clean
