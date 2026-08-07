# Software floating-point support for the PC-98 Bootstrap Environment.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: GPL-2.0-or-later
#
# GCC soft-fp files are compiled directly from the managed GCC 14.3 source
# tree.  musl math and scanner files are compiled directly from the managed
# musl 1.2.6 source tree.  Their original source notices remain authoritative;
# the binary distribution carries GCC COPYING.LIB, musl COPYRIGHT, and the
# Noct license. Keep both lists explicit for provenance review.

BOOT98_SOFTFLOAT_BUILD_DIR ?= ../build/bootloader/softfloat
BOOT98_GCC_ROOT ?= ../toolchain/gcc
BOOT98_MUSL_ROOT ?= ../toolchain/musl
BOOT98_SOFTFLOAT_CC ?= $(CC)
BOOT98_SOFTFLOAT_OBJDUMP ?= objdump

BOOT98_GCC_SOFTFP_REL := \
	adddf3.c addsf3.c divdf3.c divsf3.c eqdf2.c eqsf2.c \
	extendsfdf2.c fixdfdi.c fixdfsi.c fixsfdi.c fixsfsi.c \
	fixunsdfdi.c fixunsdfsi.c fixunssfdi.c fixunssfsi.c \
	floatdidf.c floatdisf.c floatsidf.c floatsisf.c \
	floatundidf.c floatundisf.c floatunsidf.c floatunsisf.c \
	gedf2.c gesf2.c ledf2.c lesf2.c muldf3.c mulsf3.c \
	subdf3.c subsf3.c truncdfsf2.c unorddf2.c unordsf2.c

BOOT98_MUSL_MATH_REL := \
	sinf.c cosf.c tanf.c sqrtf.c \
	__sindf.c __cosdf.c __tandf.c __rem_pio2f.c __rem_pio2_large.c \
	__math_invalidf.c sqrt_data.c fmod.c scalbn.c floor.c

BOOT98_MUSL_SCAN_REL := \
	src/internal/shgetc.c src/internal/floatscan.c src/stdlib/strtod.c

BOOT98_GCC_SOFTFP_OBJECTS := $(addprefix $(BOOT98_SOFTFLOAT_BUILD_DIR)/gcc-,\
	$(BOOT98_GCC_SOFTFP_REL:.c=.o))
BOOT98_MUSL_MATH_OBJECTS := $(addprefix $(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-,\
	$(BOOT98_MUSL_MATH_REL:.c=.o))
BOOT98_MUSL_SCAN_OBJECTS := \
	$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-shgetc.o \
	$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-floatscan.o \
	$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-strtod.o
BOOT98_SOFTFLOAT_COMPAT_OBJECT := \
	$(BOOT98_SOFTFLOAT_BUILD_DIR)/boot98-musl-compat.o
BOOT98_SOFTFLOAT_TEST := \
	../build/bootloader/tests/boot98-softfloat-host-test
BOOT98_SOFTFLOAT_OBJECTS := $(BOOT98_GCC_SOFTFP_OBJECTS) \
	$(BOOT98_MUSL_MATH_OBJECTS) $(BOOT98_MUSL_SCAN_OBJECTS) \
	$(BOOT98_SOFTFLOAT_COMPAT_OBJECT)

BOOT98_GCC_SOFTFP_CPPFLAGS := \
	-nostdinc -Ilibc/include -Ilibc \
	-I$(BOOT98_GCC_ROOT)/include -I$(BOOT98_GCC_ROOT)/libgcc \
	-I$(BOOT98_GCC_ROOT)/libgcc/config/i386 \
	-I$(BOOT98_GCC_ROOT)/libgcc/soft-fp -D_SOFT_FLOAT

BOOT98_MUSL_CPPFLAGS := \
	-nostdinc -Isoftfloat/include -Ilibc/include -Ilibc \
	-I$(BOOT98_MUSL_ROOT)/src/internal -I$(BOOT98_MUSL_ROOT)/src/math

BOOT98_SOFTFLOAT_CFLAGS := $(BOOT98_LIBC_CFLAGS) -mlong-double-64
BOOT98_MUSL_CFLAGS := $(BOOT98_SOFTFLOAT_CFLAGS) \
	-Wno-error=unused-but-set-variable

# GCC soft-fp intentionally shares a signed/unsigned conversion macro.  GCC
# diagnoses its dead sign test for the four unsigned input translations.
$(BOOT98_SOFTFLOAT_BUILD_DIR)/gcc-floatundidf.o \
$(BOOT98_SOFTFLOAT_BUILD_DIR)/gcc-floatundisf.o \
$(BOOT98_SOFTFLOAT_BUILD_DIR)/gcc-floatunsidf.o \
$(BOOT98_SOFTFLOAT_BUILD_DIR)/gcc-floatunsisf.o: \
	BOOT98_SOFTFLOAT_WARNING_EXCEPTIONS := -Wno-error=type-limits

$(BOOT98_SOFTFLOAT_BUILD_DIR)/gcc-%.o: \
	$(BOOT98_GCC_ROOT)/libgcc/soft-fp/%.c
	@mkdir -p $(BOOT98_SOFTFLOAT_BUILD_DIR)
	$(BOOT98_SOFTFLOAT_CC) $(BOOT98_GCC_SOFTFP_CPPFLAGS) \
		$(BOOT98_SOFTFLOAT_CFLAGS) \
		$(BOOT98_SOFTFLOAT_WARNING_EXCEPTIONS) -c $< -o $@

$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-%.o: \
	$(BOOT98_MUSL_ROOT)/src/math/%.c
	@mkdir -p $(BOOT98_SOFTFLOAT_BUILD_DIR)
	$(BOOT98_SOFTFLOAT_CC) $(BOOT98_MUSL_CPPFLAGS) \
		$(BOOT98_MUSL_CFLAGS) -c $< -o $@

$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-shgetc.o: \
	$(BOOT98_MUSL_ROOT)/src/internal/shgetc.c \
	softfloat/boot98-musl-floatscan.h
	@mkdir -p $(BOOT98_SOFTFLOAT_BUILD_DIR)
	$(BOOT98_SOFTFLOAT_CC) $(BOOT98_MUSL_CPPFLAGS) \
		$(BOOT98_MUSL_CFLAGS) \
		-Wno-error=parentheses \
		-include softfloat/boot98-musl-floatscan.h -c $< -o $@

$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-floatscan.o: \
	$(BOOT98_MUSL_ROOT)/src/internal/floatscan.c \
	softfloat/boot98-musl-floatscan.h
	@mkdir -p $(BOOT98_SOFTFLOAT_BUILD_DIR)
	$(BOOT98_SOFTFLOAT_CC) $(BOOT98_MUSL_CPPFLAGS) \
		$(BOOT98_MUSL_CFLAGS) \
		-Wno-error=parentheses -Wno-error=sign-compare \
		-include softfloat/boot98-musl-floatscan.h -c $< -o $@

$(BOOT98_SOFTFLOAT_BUILD_DIR)/musl-strtod.o: \
	$(BOOT98_MUSL_ROOT)/src/stdlib/strtod.c \
	softfloat/boot98-musl-floatscan.h
	@mkdir -p $(BOOT98_SOFTFLOAT_BUILD_DIR)
	$(BOOT98_SOFTFLOAT_CC) $(BOOT98_MUSL_CPPFLAGS) \
		$(BOOT98_MUSL_CFLAGS) \
		-include softfloat/boot98-musl-floatscan.h -c $< -o $@

$(BOOT98_SOFTFLOAT_COMPAT_OBJECT): softfloat/boot98-musl-compat.c \
	softfloat/boot98-musl-floatscan.h
	@mkdir -p $(BOOT98_SOFTFLOAT_BUILD_DIR)
	$(BOOT98_SOFTFLOAT_CC) $(BOOT98_MUSL_CPPFLAGS) \
		$(BOOT98_MUSL_CFLAGS) \
		-include softfloat/boot98-musl-floatscan.h -c $< -o $@

boot98-softfloat-objects: $(BOOT98_SOFTFLOAT_OBJECTS)

boot98-softfloat-opcode-check: boot98-softfloat-objects
	@if $(BOOT98_SOFTFLOAT_OBJDUMP) -d --no-show-raw-insn \
		$(BOOT98_SOFTFLOAT_OBJECTS) | \
		grep -E '(^[[:space:]]*[0-9a-f]+:[[:space:]]+f[a-z0-9]+[[:space:]])|\b(bswap|cmpxchg|xadd|cmov[a-z]*|rdtsc|ud2|cpuid|fx[a-z]+|movaps|movups|xmm[0-9]|ymm[0-9]|zmm[0-9])\b'; then \
		echo "ERROR: soft-float objects contain a post-i386 opcode" >&2; \
		exit 1; \
	fi
	@echo "BOOT98 soft-float i386 opcode check: PASS"

$(BOOT98_SOFTFLOAT_TEST): tests/boot98-softfloat-host-test.c \
	$(BOOT98_LIBC_OBJECTS) $(BOOT98_SOFTFLOAT_OBJECTS)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(BOOT98_LIBC_CPPFLAGS) $(BOOT98_SOFTFLOAT_CFLAGS) \
		-no-pie \
		tests/boot98-softfloat-host-test.c $(BOOT98_LIBC_OBJECTS) \
		$(BOOT98_SOFTFLOAT_OBJECTS) -o $@

boot98-softfloat-host-test: $(BOOT98_SOFTFLOAT_TEST)
	$(BOOT98_SOFTFLOAT_TEST)
	@echo "BOOT98 soft-float known-vector tests: PASS"

boot98-softfloat-clean:
	rm -f $(BOOT98_SOFTFLOAT_OBJECTS) $(BOOT98_SOFTFLOAT_TEST)
	@rmdir $(BOOT98_SOFTFLOAT_BUILD_DIR) 2>/dev/null || true

.PHONY: boot98-softfloat-objects boot98-softfloat-opcode-check \
	boot98-softfloat-host-test boot98-softfloat-clean
