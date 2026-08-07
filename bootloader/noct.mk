# Selected Noct core for the PC-98 Bootstrap Environment.
#
# M2 deliberately compiled these objects without linking them into BOOT.SYS.
# M3 kept that boundary and performed a relocatable link audit.  M4 links the
# same JIT-disabled core into BOOT.SYS and exercises its lifecycle.
# Generated lexer/parser C sources are imported and used directly, so flex and
# bison are not build dependencies.

NOCT_ROOT ?= ../third_party/noct
NOCT_ENABLE_JIT ?= 0
NOCT_PROFILE := $(if $(filter 1,$(NOCT_ENABLE_JIT)),jit,nojit)
NOCT_BUILD_DIR ?= ../build/bootloader/noct-$(NOCT_PROFILE)
NOCT_CC ?= $(CC)
NOCT_OBJDUMP ?= objdump
NOCT_SIZE ?= size
NOCT_LD ?= $(LD)
NOCT_NM ?= nm

NOCT_SOURCE_REL := \
	src/core/lexer.yy.c \
	src/core/parser.tab.c \
	src/core/ast.c \
	src/core/hir.c \
	src/core/lir.c \
	src/core/noct.c \
	src/core/runtime.c \
	src/core/interpreter.c \
	src/core/jit.c \
	src/core/execution.c \
	src/core/gc.c \
	src/core/intrinsics.c \
	src/core/objectmodel-st.c

NOCT_SOURCES := $(addprefix $(NOCT_ROOT)/,$(NOCT_SOURCE_REL))
NOCT_OBJECTS := $(patsubst $(NOCT_ROOT)/src/core/%.c,$(NOCT_BUILD_DIR)/%.o,$(NOCT_SOURCES))
NOCT_UPSTREAM_COMMIT := $(shell sed -n 's/^Commit: `\([^`]*\)`.*/\1/p' $(NOCT_ROOT)/UPSTREAM.md)

NOCT_CPPFLAGS := \
	-nostdinc \
	-Ilibc/include \
	-Ilibc \
	-I$(NOCT_ROOT)/include \
	-I$(NOCT_ROOT)/src/core \
	-DNOCT_TARGET_PC98BE \
	-DNOCT_MEMORY_SMALL \
	-DHAVE_STDINT_H=1 \
	-DHAVE_INTTYPES_H=1 \
	-DHAVE_SYS_TYPES_H=1 \
	-DHAVE_STDBOOL_H=1

ifeq ($(NOCT_ENABLE_JIT),1)
NOCT_CPPFLAGS += -DNOCT_USE_JIT
endif

NOCT_CFLAGS := \
	-m32 -march=i386 -Os -ffreestanding -fno-builtin \
	-fno-pic -fno-pie -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-isolate-erroneous-paths-dereference -fno-strict-aliasing \
	-msoft-float -mno-80387 -mno-fp-ret-in-387 \
	-mno-mmx -mno-sse -mno-sse2 \
	-Wall -Wextra -Werror

# The approved upstream snapshot predates a warning-clean release build.
# Keep -Werror for every other diagnostic, but leave these exact warnings
# visible in the named translation units so the vendored snapshot can remain
# byte-for-byte identical to upstream.
$(NOCT_BUILD_DIR)/noct.o: NOCT_WARNING_EXCEPTIONS := \
	-Wno-error=unused-parameter
$(NOCT_BUILD_DIR)/runtime.o: NOCT_WARNING_EXCEPTIONS := \
	-Wno-error=maybe-uninitialized
$(NOCT_BUILD_DIR)/jit.o: NOCT_WARNING_EXCEPTIONS := \
	-Wno-error=unused-parameter -Wno-error=sign-compare
$(NOCT_BUILD_DIR)/intrinsics.o: NOCT_WARNING_EXCEPTIONS := \
	-Wno-error=type-limits
$(NOCT_BUILD_DIR)/objectmodel-st.o: NOCT_WARNING_EXCEPTIONS := \
	-Wno-error=maybe-uninitialized

$(NOCT_BUILD_DIR)/%.o: $(NOCT_ROOT)/src/core/%.c
	@mkdir -p $(NOCT_BUILD_DIR)
	$(NOCT_CC) $(NOCT_CPPFLAGS) $(NOCT_CFLAGS) \
		$(NOCT_WARNING_EXCEPTIONS) -c $< -o $@

noct-objects: $(NOCT_OBJECTS)
	@echo "Noct upstream: $(NOCT_UPSTREAM_COMMIT)"
	@$(NOCT_SIZE) --totals $(NOCT_OBJECTS) | tail -1

noct-opcode-check: noct-objects
	@if $(NOCT_OBJDUMP) -d --no-show-raw-insn $(NOCT_OBJECTS) | \
		grep -E '(^[[:space:]]*[0-9a-f]+:[[:space:]]+f[a-z0-9]+[[:space:]])|\b(bswap|cmpxchg|xadd|cmov[a-z]*|rdtsc|ud2|cpuid|fx[a-z]+|movaps|movups|xmm[0-9]|ymm[0-9]|zmm[0-9])\b'; then \
		echo "ERROR: selected Noct objects contain a post-i386 opcode" >&2; \
		exit 1; \
	fi
	@echo "Noct i386 opcode check: PASS"

NOCT_M3_RELOC := ../build/bootloader/noct-libc-m3.o
NOCT_M3_UNDEFINED := ../build/bootloader/noct-libc-m3.undefined

noct-link-audit: noct-objects boot98-libc-objects
	@mkdir -p $(dir $(NOCT_M3_RELOC))
	$(NOCT_LD) -m elf_i386 -r $(NOCT_OBJECTS) $(BOOT98_LIBC_OBJECTS) \
		-o $(NOCT_M3_RELOC)
	@$(NOCT_NM) -u $(NOCT_M3_RELOC) | awk '{print $$NF}' | sort -u > \
		$(NOCT_M3_UNDEFINED)
	@if grep -Ev -f libc/deferred-symbols.regex $(NOCT_M3_UNDEFINED) | \
		grep -q .; then \
		echo "ERROR: unexpected undefined symbols in Noct/libc M3 object:" >&2; \
		grep -Ev -f libc/deferred-symbols.regex $(NOCT_M3_UNDEFINED) >&2; \
		exit 1; \
	fi
	@echo "Noct/libc unresolved-symbol audit: PASS"
	@if test -s $(NOCT_M3_UNDEFINED); then \
		echo "Deferred soft-float/math symbols:"; \
		sed 's/^/  /' $(NOCT_M3_UNDEFINED); \
	else \
		echo "Deferred soft-float/math symbols: none"; \
	fi

noct-m3-verify: boot98-libc-host-test boot98-libc-opcode-check \
	noct-opcode-check noct-link-audit
	@echo "PC98BE M3 verification: PASS (Noct JIT disabled)"

noct-clean:
	rm -f $(NOCT_OBJECTS) $(NOCT_M3_RELOC) $(NOCT_M3_UNDEFINED)
	@rmdir $(NOCT_BUILD_DIR) 2>/dev/null || true

.PHONY: noct-objects noct-opcode-check noct-link-audit noct-m3-verify \
	noct-clean
