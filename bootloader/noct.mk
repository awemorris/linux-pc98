# Selected Noct core for the PC-98 Bootstrap Environment.
#
# M2 deliberately compiles these objects without linking them into BOOT.SYS.
# Generated lexer/parser C sources are imported and used directly, so flex and
# bison are not build dependencies.

NOCT_ROOT ?= ../third_party/noct
NOCT_BUILD_DIR ?= ../build/bootloader/noct
NOCT_CC ?= $(CC)
NOCT_OBJDUMP ?= objdump
NOCT_SIZE ?= size

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
	-I$(NOCT_ROOT)/include \
	-I$(NOCT_ROOT)/src/core \
	-DNOCT_TARGET_PC98BE \
	-DNOCT_MEMORY_SMALL \
	-DNOCT_USE_JIT \
	-DHAVE_STDINT_H=1 \
	-DHAVE_INTTYPES_H=1 \
	-DHAVE_SYS_TYPES_H=1

NOCT_CFLAGS := \
	-m32 -march=i386 -Os -DNDEBUG -ffreestanding \
	-fno-pic -fno-pie -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-isolate-erroneous-paths-dereference -fno-strict-aliasing \
	-msoft-float -mno-80387 -mno-fp-ret-in-387 \
	-mno-mmx -mno-sse -mno-sse2 \
	-Wall -Wextra -Werror

# The approved upstream snapshot predates a warning-clean release build.
# Keep -Werror for every other diagnostic, but leave these exact warnings
# visible in the named translation units so M2 can remain a byte-for-byte
# source import.  They must be fixed upstream before the M3 link milestone.
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

noct-clean:
	rm -f $(NOCT_OBJECTS)
	@rmdir $(NOCT_BUILD_DIR) 2>/dev/null || true

.PHONY: noct-objects noct-opcode-check noct-clean
