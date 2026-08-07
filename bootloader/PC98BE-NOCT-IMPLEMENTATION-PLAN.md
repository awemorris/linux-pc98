# Boots: Noct Integration and Finishing Plan

Status: **APPROVED — IMPLEMENT ONE REVIEWED MILESTONE AT A TIME**

Approval recorded: 2026-08-07.  Implementation begins with M0 and must retain
the review gates in Section 18; approval of the overall plan is not approval
to combine later milestones into one changeset.

Prepared against:

- `linux-pc98` commit `6dbd7d6f199f44fe5ed47c3f7e3ec11104c14dd4`
- NoctLang commit `40670781403c760a0793cfb324b2b53a4c2f0228`
- Date: 2026-08-07

Finishing requirements amended 2026-08-08 against:

- `linux-pc98` commit `d47dc9904`
- NoctLang commit `765df9ed88439eed91d118ab9bdbc6d442524527`

The amendment replaces the embedded Noct subtree with a pinned submodule,
adds the reusable REPL and Boots environment-variable work, and removes the
planned in-tree editor implementation. Remacs is the separately maintained
Noct editor selected for later Boots compatibility and packaging work.

This document is intentionally prescriptive. It is written so that an
implementation model can execute one reviewable milestone at a time without
reopening the architectural decisions. If the source has moved since the
commits above, first refresh the baseline inventory and update the affected
file names in this document; do not silently apply the plan to a substantially
different tree.

## 1. Objective

Extend the existing PC-98 `BOOT.SYS` into **Boots**, whose formal expansion is
**Boot loader and pre-boot environment**. Boots can execute Noct scripts and
return to its shell. The source directory remains `bootloader/`; established
disk filenames (`IO.SYS`, `BOOT.SYS`) and internal `boot98_*`/`PC98BE` target
identifiers remain stable unless a separately reviewed ABI-neutral rename is
worth the churn.

The completed first release must provide all of the following:

1. Noct source is pinned as a `third_party/noct` git submodule, with its zlib
   license and exact upstream revision recorded.
2. The Noct core executes in 32-bit protected mode on an i386 without an FPU.
3. The x86 JIT is enabled and executes on QEMU `-cpu 386` without i486, x87,
   MMX, or SSE instructions.
4. A script is invoked explicitly as `noct FILE.NCT [args...]` or implicitly
   by entering the body name of a root-directory `NAME.NCT` file.
5. Returning from `main()` or encountering a normal compile/runtime error
   returns to the BE shell, restores the terminal cursor, closes files, and
   reclaims the complete per-script arena.
6. Safe Native APIs cover console output, positional screen output, keyboard
   input, directory enumeration, and ordinary file access.
7. FAT16 supports the minimum write operations needed by Noct's standard
   `File` API, `cp`, and later Remacs compatibility.
8. `ls.nct` and `cp.nct` are included as examples; the separately maintained
   Noct program Remacs supplies the screen editor after compatibility tests.
9. `dd.nct` is implemented only after a bounded raw-block NAPI exists.
10. The existing menu, `BOOT.CFG`, Linux boot, chain boot, applets, and
    IPLware behavior continue to pass regression tests.
11. The shell can start a Noct REPL. One VM is created for the REPL session
    and destroyed completely when Ctrl-C returns to the Boots shell.
12. Boots environment variables are available to BOOT.CFG, the shell, normal
    Noct scripts, and the REPL without retaining pointers into a Noct VM.
13. Boots-owned source is released under the zlib license after a per-file
    provenance audit; imported third-party notices and licenses are retained.

The kernel remains a non-returning payload. Ordinary Noct scripts are
returning Boots programs and use one VM per invocation. The REPL is the one
deliberate session-scoped exception: its VM persists only from REPL entry to
Ctrl-C and never survives the return to the Boots shell.

## 2. Explicit non-goals for the first integration

- No multithreaded Noct object model.
- No complete Noct CLI, i18n, C/bytecode/Elisp/Scheme backends, process
  creation, host shell execution, or POSIX emulation. Only the reusable REPL
  session engine is integrated.
- No paging, process isolation, privilege rings, or claim that scripts are a
  security boundary. Scripts on the BOOT partition are trusted code.
- No FAT32, ext4, or UFS writer in this project phase. The generic interfaces
  must permit them later.
- No subdirectory creation, long filename creation, journaling, or atomic
  rename in the first FAT16 writer.
- No persistent VM or module cache across shell commands. A REPL VM is scoped
  to that one interactive command and is destroyed on exit.
- No `Unsafe.Memory`, `Unsafe.IO`, or `Unsafe.BIOS` API in the first safe API
  milestone. Their namespaces are reserved for a separately reviewed phase.
- No FreeBSD kernel loader in this plan. The image-loader abstraction remains
  ready for a later `freebsd` command.
- Do not solve BOOT.SYS relocation before measurements prove it necessary.

## 3. Settled architecture

### 3.1 Boot chain

The current stages remain unchanged:

```text
LBA 0                    ipl-lba0.bin
  -> LBA 2               ipl-lba2.bin or NEC fixed-disk menu
     -> BOOT PBR         ipl-part.img
        -> IO.SYS        real-mode FAT16 loader and BIOS gateway
           -> BOOT.SYS   32-bit BE, shell, Noct, and kernel loader
```

No Noct code belongs in the PBR or `IO.SYS`. BIOS calls continue through the
existing protected-mode-to-real-mode gateway owned by `IO.SYS`.

### 3.2 Resident and ephemeral state

`BOOT.SYS` contains the resident BE code and static Noct core/JIT code. Each
script invocation creates an ephemeral runtime:

```text
resident BOOT.SYS
  -> initialize high-memory script arena
  -> create Noct VM and default environment
  -> register selected NAPIs
  -> load source and invoke main
  -> destroy VM
  -> close host resources that remain open
  -> reset complete script arena in O(1)
  -> restore BE console state and return status to shell
```

No pointer into the script arena may be retained by the BE after reset.
Persistent disk selection, partition selection, kernel name, and kernel
arguments stay in the existing BE globals outside the script arena.

### 3.3 Source ownership boundary

Generic Noct improvements belong upstream in `awemorris/NoctLang`:

- `NOCT_TARGET_PC98BE` target recognition.
- allocator-hook completeness, including `noct_realloc`.
- removal of accidental raw allocator calls in the core.
- generic freestanding/JIT portability hooks.
- `NOCT_MEMORY_TINY`, after measurements establish useful defaults.
- corrections to Noct API documentation and target-independent File API bugs.
- a reusable REPL session engine and robust multiline-input recognition.
- Linux CLI signal/input glue needed to exit the REPL cleanly with Ctrl-C.

PC-98 and BE host glue belongs in `linux-pc98`:

- BIOS gateway use.
- physical memory map selection.
- BE allocator implementation.
- freestanding libc/stdio implementation.
- PC-98 text VRAM and keyboard NAPIs.
- generic BOOT98 filesystem adapter and FAT16 write support.
- disk/block APIs, shell dispatch, scripts, and image integration.

Do not maintain original, modified, and patch-only Noct trees in parallel.
The user is the sole upstream author and has authorized upstream changes.
Generic changes are committed to NoctLang first and then imported.

### 3.4 Noct import format

Noct is pinned as the `third_party/noct/` git submodule. A submodule is chosen
over the earlier squashed subtree because Noct is receiving rapid fixes and
features: the gitlink makes the reviewed upstream boundary exact, avoids
copying upstream commits into an opaque vendor snapshot, and permits one-step
updates after Noct's own tests pass.

```text
.gitmodules                    upstream URL
third_party/noct               gitlink pinned to an exact Noct commit
scripts/update-noct.sh         init/status/update/verify maintainer commands
```

`./build.sh noct init` is the only normal operation that may need the network.
Once initialized, all product, image, and release builds are offline and must
never fetch implicitly. `./build.sh noct update [REF]` fetches explicitly,
checks out a detached upstream revision, and stages only the gitlink. Release
source archives must either include the initialized submodule content or state
the required recursive clone command; a bare superproject archive is not a
complete Boots source distribution.

Preserve Noct's zlib `LICENSE` and source notices. Generic Noct changes are
committed and tested in NoctLang first; linux-pc98 then advances only the
gitlink. Direct edits inside the submodule are rejected by verification.

### 3.5 Boots naming and license boundary

The public product name is **Boots** and its expansion is **Boot loader and
pre-boot environment**. User-visible headings, manuals, release notes, and
new APIs use Boots. The historical `BOOT98` disk labels, executable names,
`boot98_*` C symbols, and `NOCT_TARGET_PC98BE` may remain as implementation
identifiers to avoid unnecessary disk-format or source-ABI changes.

Boots-owned code is to be relicensed to zlib in a dedicated mechanical
milestone. Before changing headers, generate a manifest of every file under
`bootloader/`, confirm sole Awe Morris authorship or an explicit relicenseable
origin, then add `SPDX-License-Identifier: Zlib` and the Boots zlib LICENSE.
Do not rewrite notices in `third_party/noct`, GCC soft-fp sources, musl math
sources, or any other imported component. Those components retain their own
licenses and appear separately in the source and binary release inventory.

## 4. Current constraints that implementation must preserve

### 4.1 Current BOOT.SYS layout

`bootloader/boot98-stage2.ld` currently provides:

| Address | Current use |
|---:|---|
| `0x00020000` | BOOT.SYS header and load image start |
| `0x00060000` | End of the current 256 KiB load-image window |
| `0x00070000` | `boot_params`; BOOT.SYS BSS must end below here |
| `0x00071000` | Linux command line |
| `0x00072000` | PC-98 setup-data node |
| `0x0008f000` | protected-mode stack area |
| `0x00100000` | first Linux ELF `PT_LOAD` segment and initial script arena |

The existing BOOT.SYS is approximately 14 KiB, so the present linker limit
offers about 242 KiB of additional load-image space, not 256 KiB in addition
to the existing image. Every milestone must report:

```text
BOOT.SYS file bytes
resident .text/.rodata/.data bytes
resident BSS bytes
remaining bytes before 0x60000 and 0x70000
```

Do not increase the on-disk `BOOT.SYS` size limit or change `IO.SYS` until a
link map proves the existing window insufficient.

### 4.2 Product source subset

Compile only the following Noct components into the initial BE:

- generated lexer and parser sources already committed upstream;
- AST, HIR, LIR;
- public Noct API and runtime;
- interpreter, execution helpers, GC, and intrinsics;
- single-threaded object model;
- common JIT plus x86 JIT;
- standard `api-file.c` only after BE stdio is ready.

Exclude CLI, REPL, translation, all optional compiler backends,
`api-system.c`, `api-console.c`, and `api-thread.c`. BE supplies a smaller
Console API and a deliberately limited System/import API.

### 4.3 Compiler baseline

All resident and runtime code must be built with the equivalent of:

```text
-m32 -march=i386 -Os -ffreestanding
-fno-pic -fno-pie -fno-stack-protector
-fno-asynchronous-unwind-tables -fno-unwind-tables
-msoft-float -mno-80387 -mno-fp-ret-in-387 -mno-mmx -mno-sse -mno-sse2
```

Use `-Wall -Wextra -Werror`. Fix warnings; do not add broad warning
suppressions. Link without a host libc. Never reuse the DOS4G OpenWatcom
`-fpi87` configuration for PC98BE.

## 5. Memory plan

### 5.1 SMALL baseline

At the inspected Noct revision, `NOCT_MEMORY_SMALL` reserves at least:

| Region | Bytes |
|---|---:|
| GC nursery | 256 KiB |
| GC graduate from-space | 64 KiB |
| GC graduate to-space | 64 KiB |
| GC tenure | 1 MiB |
| JIT code maximum on DOS-like target | 1 MiB |
| subtotal | 2.375 MiB |

VM structures, lexer/parser/HIR/LIR allocations, source text, file buffers,
and allocator metadata are additional. Treat 3–4 MiB as the initial practical
budget until measured.

### 5.2 Script arena placement

The initial arena is physical RAM beginning at `0x00100000`. Its upper bound
is derived from the same BIOS memory information used to construct the Linux
e820 map. It must be clamped to actual contiguous RAM and must not cross a
reported or known 15–16 MiB hole.

Selection rules:

1. On machines with RAM ending below 15 MiB, use `[1 MiB, RAM end)` minus a
   small guard page-equivalent margin.
2. On larger machines, initially cap the arena below 15 MiB. This is enough
   for SMALL and avoids depending on 15–16 MiB latch state.
3. Do not use BOOT.SYS low memory, boot parameters, BIOS work areas, VRAM, or
   the protected-mode stack as allocator storage.
4. Before loading a kernel, destroy/reset the Noct runtime. The kernel may
   then overwrite memory at and above 1 MiB.

The first implementation may reject script execution with a clear message if
the contiguous arena is too small. It must never try to continue after a
partial VM allocation.

### 5.3 BE allocator

Add a bounded allocator with these operations:

```c
void boot98_heap_init(void *base, size_t size);
void boot98_heap_reset(void);
void *boot98_malloc(size_t size);
void *boot98_calloc(size_t count, size_t size);
void *boot98_realloc(void *pointer, size_t size);
char *boot98_strdup(const char *string);
void boot98_free(void *pointer);
size_t boot98_heap_current(void);
size_t boot98_heap_peak(void);
```

Requirements:

- 8-byte alignment.
- overflow checks for add/multiply/align operations.
- free-list allocation with adjacent-block coalescing; not bump-only.
- deterministic behavior and no recursion.
- invalid/double-free detection in debug builds.
- whole-arena O(1) logical reset after every script.
- peak and current-byte counters for tests.
- failed allocation returns `NULL` and does not corrupt the list.

Noct's `noct_malloc`, `noct_calloc`, `noct_realloc`, `noct_strdup`, and
`noct_free` macros all map to these functions for PC98BE.

### 5.4 TINY profile

Do not invent final TINY values before SMALL measurements. Begin measurement
with this explicit candidate:

| Region | Candidate value |
|---|---:|
| nursery | 128 KiB |
| graduate, each semi-space | 32 KiB |
| tenure | 512 KiB |
| JIT maximum | 256 KiB |

Expose GC sizes through `NoctConfig`; make the JIT maximum configurable or
target-specific rather than duplicating an unrelated DOS4G condition.

TINY is accepted only if the syntax regression corpus, File API smoke test,
and later Remacs compatibility tests run repeatedly without corruption. A clean OOM is
acceptable for a script too large for TINY. Silent truncation or reduced
language semantics is not acceptable.

## 6. Freestanding C runtime

Place BE runtime code under:

```text
bootloader/libc/include/
bootloader/libc/
bootloader/softfloat/
```

Do not modify global host headers. The Noct PC98BE compile includes this
directory before any host include directory and uses only the explicitly
provided freestanding headers.

### 6.1 Required header/API inventory

Provide the subset actually referenced by the selected Noct source:

- `assert.h`, `ctype.h`, `errno.h`, `inttypes.h`, `limits.h`;
- `math.h`, `stdarg.h`, `stdbool.h`, `stddef.h`, `stdint.h`;
- `stdio.h`, `stdlib.h`, `string.h`, `time.h`, `unistd.h`.

Each header must declare only implemented behavior. Do not paste a complete
host libc header with unsupported declarations.

### 6.2 Required functions

Implement and host-test at least:

- memory/string: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`,
  `strnlen`, `strcmp`, `strncmp`, `strcpy`, `strncpy`, `strcat`, `strncat`,
  `strchr`, `strrchr`, `strstr`, `strdup`;
- classification/conversion: the required `ctype` operations, `atoi`,
  `strtol`, `strtoul`, `atof`/`strtod`;
- formatting: `snprintf`, `vsnprintf`; `%s`, `%c`, signed/unsigned integer,
  hexadecimal, width/precision used by Noct, `PRId64`, and the float formats
  actually used by Noct (`%.7g`, `%.15g`);
- allocation: functions in Section 5.3;
- stdio calls listed in Section 10;
- `access(path, 0)` as a filesystem existence query;
- minimal `time()` only if the selected Noct core still references it after
  dead code is excluded.

Formatting must always NUL-terminate when the buffer size is nonzero and must
return the number of bytes that would have been written, matching C99
`snprintf` semantics. Add focused tests before Noct depends on it.

### 6.3 Assertions and failures

In development builds, `assert()` prints file and line and returns control to
the script host through a fatal-VM status where possible. A corrupted runtime
may not safely continue; if recovery is impossible, halt with a diagnostic
rather than returning to the BE with damaged state. Release builds may use
`NDEBUG` only after all Noct tests pass with assertions enabled.

## 7. Soft-float plan

The system i386 libgcc inspected on the build host does not provide the
required `__addsf3`/`__adddf3` family. Therefore `-msoft-float` is necessary
but not sufficient.

Use the managed GCC source under `toolchain/gcc/libgcc/soft-fp/` and its i386
`sfp-machine.h` as the primary implementation for compiler ABI operations.
Compile with `_SOFT_FLOAT` and the same i386 flags as BOOT.SYS. Start from the
exact unresolved-symbol inventory produced by a Noct link and include only
the required modules, expected to include:

```text
add/sub/mul/div: sf and df
compare/unordered: sf and df
signed/unsigned integer conversions
sf <-> df conversions
float/double -> integer conversions
negation where emitted
```

Use selected, license-preserved musl sources under `toolchain/musl/` for
`strtod`/`atof`, `sqrt[f]`, `sin[f]`, `cos[f]`, and `tan[f]`, with their
transitive math helpers. Compile those sources with the same software-float
ABI so their arithmetic resolves to the soft-fp functions.

Rules:

1. Keep an explicit source list in `bootloader/softfloat/softfloat.mk`; do not
   glob entire GCC or musl directories.
2. Preserve GCC Runtime Library Exception and musl license notices in the
   release inventory.
3. Add an isolated C arithmetic/conversion/math test before linking Noct.
4. Disassemble every produced object and reject x87/MMX/SSE/i486+ opcodes.
5. Compare a deterministic vector of normal, zero, negative, infinity, NaN,
   overflow, and underflow results against a host build.
6. Do not silently disable Noct float/double values. If transcendental math
   is temporarily incomplete, stop at the review gate rather than declaring
   the Noct integration complete.

Fallback only if GCC soft-fp cannot be made to compile cleanly: propose a
separately licensed Berkeley SoftFloat import to the user. Do not choose the
fallback without review.

## 8. Noct upstream change list

Apply generic changes in the NoctLang repository, with upstream tests passing
after every commit.

### 8.1 Target definition

Modify:

- `CMakeLists.txt`
- `CMakePresets.json` or add a documented PC98BE preset
- `include/noct/c89compat.h`
- add `cmake/toolchains/pc98be-i386.cmake` if upstream standalone validation
  uses CMake

Required behavior:

- define and recognize `NOCT_TARGET_PC98BE`;
- define `NOCT_ARCH_X86`, little endian through normal `__i386__` detection;
- do not define POSIX, DOS4G, or Windows behavior;
- select single-threaded model, SMALL initially, JIT enabled, API components
  individually selectable;
- no target-detection `#error` for PC98BE.

### 8.2 Allocator portability

Modify:

- `include/noct/noct.h`
- `src/core/arena.h`
- any selected source revealed by `git grep` to call raw allocation functions

Required behavior:

- add a `noct_realloc` hook;
- use `noct_free` in `arena_cleanup` rather than raw `free`;
- selected core sources use the complete Noct allocator macro family;
- standard APIs use Noct allocation hooks for their temporary buffers where
  ownership is internal to Noct;
- host targets retain their current defaults.

### 8.3 JIT platform hook

Modify `src/core/jit.c` and `src/core/jit.h` so PC98BE:

- obtains the JIT region from the BE allocator;
- treats writable/executable transitions as no-ops because the flat mapping
  has no paging/W^X policy;
- performs no `mmap`, `mprotect`, `munmap`, DPMI, or host OS call;
- releases the region through the allocator during VM destruction;
- uses a PC98BE/TINY-configurable maximum, not the DOS4G branch by accident.

x86 has coherent instruction and data caches, so no cache-flush instruction
is needed. Keep the platform hook explicit for documentation.

### 8.4 File API correctness

Fix target-independent defects in `src/api/api-file.c` upstream before
importing it:

- check `fopen` before attaching or closing the pointer;
- do not call `fclose(NULL)`;
- define behavior for short read at EOF;
- validate `fseek`, `ftell`, `fread`, and `fwrite` results;
- consistently clear native pointers after close;
- use allocator hooks for temporary buffers;
- make every failure set a useful Noct error.

Do not add PC-98 filesystem calls to upstream `api-file.c`; it continues to
use the stdio contract.

### 8.5 Documentation correctness

Update `docs/napi.md` to match the current API:

- `noct_create_vm(&vm, &env, &config)`;
- `noct_destroy_vm(vm)`;
- current value enum and signatures;
- `noct_realloc` allocator hook.

This documentation correction is part of upstream readiness, not an optional
cleanup.

## 9. linux-pc98 integration layout

Add these files or equivalent narrowly named files:

```text
bootloader/noct.mk
bootloader/boot98-heap.[ch]
bootloader/boot98-noct.[ch]
bootloader/boot98-noct-napi.[ch]
bootloader/boot98-stdio.[ch]
bootloader/boot98-softfloat-test.c
bootloader/libc/include/*.h
bootloader/libc/*.c
bootloader/softfloat/softfloat.mk
bootloader/tests/boot98-heap-host-test.c
bootloader/tests/boot98-libc-host-test.c
bootloader/tests/boot98-noct-host-test.c
bootloader/tests/boot98-fat-write-host-test.c
bootloader/scripts/HELLO.NCT
bootloader/scripts/LS.NCT
bootloader/scripts/CP.NCT
bootloader/scripts/DD.NCT        # after Block API
# Remacs is packaged later after separate source/license review.
```

`bootloader/noct.mk` contains the exact imported Noct source list. Generated
lexer/parser C files are used directly, so normal builds do not require flex
or bison. Build objects into `build/bootloader/noct/`, never into the imported
source tree.

`make -C bootloader` continues to be the product entry point and links the
Noct objects into BOOT.SYS. The top-level command remains:

```sh
./build.sh bootloader
```

Add developer commands without changing release behavior:

```sh
./build.sh noct host-test
./build.sh noct opcode-check
./build.sh noct qemu-test-small
./build.sh noct qemu-test-tiny
```

## 10. stdio and standard Noct File API

Reuse Noct's standard `File` and `FileUtil` APIs. Do not create a competing
BE-only File namespace.

### 10.1 FILE representation

The private BE `FILE` object contains:

```c
struct boot98_stdio_file {
        struct boot98_file file;
        uint64_t offset;
        uint64_t size;
        unsigned mode;
        unsigned eof;
        unsigned error;
        unsigned dirty;
        struct boot98_stdio_file *next_open;
};
```

Track all open streams in a per-invocation list so cleanup closes them before
arena reset. `stdin`, `stdout`, and `stderr` may be static sentinel streams
mapped to keyboard and BE console; ordinary File NAPI handles refer to
filesystem-backed objects.

### 10.2 Initial modes and functions

Support modes needed by Noct's API:

- `rb`/`r`: existing file, read-only;
- `wb`/`w`: create or truncate, write-only;
- optionally `ab`/`a` only after append behavior is tested.

Implement:

```text
fopen fclose fflush
fread fwrite
fseek ftell
fgets
fprintf printf
access(path, 0)
```

Paths initially address the currently selected/mounted BOOT FAT16 filesystem
and its root directory. Normalize `/NAME.EXT` and `NAME.EXT` identically.
Reject `..`, subdirectories, long names, and malformed 8.3 names explicitly.

### 10.3 File lifetime

`fclose` flushes metadata and detaches the stream. The Noct native-pointer
finalizer calls it if still open. After `noct_destroy_vm`, the host closes any
remaining streams before resetting the arena. Each close path must be
idempotent from the perspective of finalizer cleanup.

## 11. Generic filesystem and FAT16 write extension

### 11.1 Generic interface

Extend `boot98-fs.h` without making callers FAT-specific:

```c
typedef int (*boot98_volume_write_t)(void *context, uint32_t lba,
                                     const void *buffer);

driver operations:
  create/open
  read/write
  truncate
  flush
  readdir
  stat
```

Keep remove/rename as optional driver methods for a later safe-save phase.
Read-only filesystems may leave write methods `NULL`; generic wrappers return
a distinct read-only error.

Replace Boolean-only internal errors with a small stable enum sufficient to
distinguish not found, invalid path, read-only, no space, I/O, corrupt
filesystem, and unsupported operation. Preserve Boolean compatibility wrappers
until existing BOOT.SYS callers are migrated.

### 11.2 BIOS write service

Add `BOOT98_BIOS_DISK_WRITE` as a new service number; do not renumber existing
services. Mirror the current one-sector read bounce-buffer path:

1. Validate caller buffer, BIOS ID, H/S, and LBA in protected mode.
2. Copy exactly 512 bytes from caller memory to the low-memory bounce buffer.
3. Enter real mode.
4. Convert LBA with that disk's BIOS logical H/S.
5. Issue the documented PC-98 fixed-disk one-sector write service.
6. Return BIOS status through the existing request/result convention.
7. For debug builds, optionally read back and compare metadata-sector writes.

Never pass a high-memory pointer directly to the BIOS. Never write more than
one physical sector per gateway request.

### 11.3 FAT16 writer

The current implementation uses 512-byte physical sectors even when the BPB
uses 1024-byte logical sectors. Preserve this model. Implement in this order:

1. writable sector cache with explicit invalidate/dirty handling;
2. read-modify-write helpers for one 512-byte physical sector;
3. FAT16 entry read/write;
4. update every FAT copy, not only the first;
5. find a free cluster with a bounded wraparound scan;
6. allocate and zero a cluster;
7. link and free cluster chains with loop/corruption bounds;
8. find/create an 8.3 root-directory entry;
9. create/truncate a file;
10. write at an offset and extend its chain;
11. update file size and first cluster;
12. flush data, FAT copies, then directory metadata.

For a newly allocated cluster, write/zero data before making the directory
entry expose the final size. Full power-loss atomicity is not promised, but
metadata ordering must avoid a directory size pointing to unallocated data.

Reject writes when:

- the BPB is not supported FAT16;
- a cluster chain loops or leaves the valid range;
- no free root entry or cluster exists;
- a request exceeds the mounted partition;
- arithmetic overflows;
- the volume has no write callback.

Add host tests with both 512-byte and 1024-byte BPB logical sectors and verify
the result using an independent host filesystem tool where practical.

## 12. Noct host lifecycle and shell dispatch

### 12.1 Host entry point

`boot98_noct_run(path, argc, argv)` performs exactly:

1. verify a mounted filesystem and bounded source size;
2. read the complete source into the script arena and NUL-terminate it;
3. initialize `NoctConfig` from SMALL/TINY build profile;
4. create VM and environment;
5. register custom Console/System, standard File, Screen, Keyboard, and
   Directory APIs;
6. register source;
7. locate `main`, query its parameter count;
8. call `main()` for zero parameters or `main(args)` for one parameter;
9. reject more than one parameter with a clear diagnostic;
10. convert an integer return value to shell status when present, otherwise
    treat normal completion as status zero;
11. print `file:line: error` on compile/runtime failure;
12. destroy VM, close remaining streams, reset arena, restore console mode and
    visible cursor, and return to the shell.

Every failure after arena initialization follows the same cleanup label.
There must be no early return that skips VM destruction/resource cleanup.

### 12.2 Command resolution

Resolution order is fixed:

1. existing C built-in command;
2. explicit `noct FILE.NCT [args...]` command;
3. if an otherwise unknown command contains no slash or dot, uppercase the
   body, append `.NCT`, and look in the root of the selected BOOT filesystem;
4. if found, invoke it with remaining shell arguments;
5. otherwise report the existing unknown-command error.

Thus `ls` resolves to `LS.NCT`; `noct TEST.NCT x` remains available for
explicit execution. Remacs will be resolved by its approved application name
after it is packaged. C built-ins keep precedence, so adding an NCT file
cannot replace `boot`, `disk`, or another recovery command.

Maximum source size begins at 256 KiB and must be configurable. Reject larger
files before allocation. Maximum shell argument count remains bounded by the
existing tokenizer and `NOCT_ARG_MAX`.

### 12.3 Console restoration

Before calling a script, record the terminal mode and logical cursor. On every
return path:

- leave fixed-screen editor drawing mode;
- set BE terminal mode;
- place the logical output cursor on a valid line;
- program GDC CSRFORM/CSRW so the cursor is visible;
- print the next BE prompt without overwriting editor content unpredictably.

## 13. Initial NAPI contract

Use dictionaries and names consistent with existing Noct APIs. All NAPI
functions validate types, ranges, sizes, and integer overflow before touching
memory or hardware. On failure, call `noct_error()` and return `false`.

### 13.1 Console

Provide a small BE implementation rather than linking upstream
`api-console.c` and its large stack serializer:

```text
Console.print(value)       serialized value plus newline
Console.write(text)        no implicit newline
```

Serialization must be bounded and iterative or depth-limited. Never allocate
an 8 KiB automatic buffer on the BE stack for every print.

### 13.2 Screen

```text
Screen.getWidth()                  -> 80
Screen.getHeight()                 -> 25
Screen.clear()                     -> 0
Screen.clearRow(row)               -> 0
Screen.put(row, column, text, attr)-> cells written
Screen.setCursor(row, column)      -> 0
Screen.showCursor(visible)         -> 0
```

`text` is Shift-JIS. Reuse `boot98-console.c` conversion rather than
duplicating it. Add bounded console primitives for raw positional writes and
attributes. A double-byte glyph may not be split at column 79.

### 13.3 Keyboard

```text
Keyboard.poll()            -> normalized key code or -1
Keyboard.read()            -> normalized key code
Keyboard.isPrintable(code) -> 0/1
```

Expose constants in a `Key` dictionary for Escape, Enter, Backspace, Delete,
arrows, Home, End, PageUp, PageDown, and function keys needed by the editor.
Keep ordinary byte values compatible with the current shell. Add a key-code
translation test using injected gateway results before relying on real
hardware.

### 13.4 Directory

```text
Directory.list(path) -> array of dictionaries
Directory.stat(path) -> dictionary or error
```

Each entry contains `name`, `size`, `attributes`, and Boolean `directory`.
Initial FAT16 supports only `/` but the NAPI remains path-based.

### 13.5 Minimal System

Do not link host `api-system.c`. Provide only:

```text
System.getOSName()     -> "PC98BE"
System.import(path)    -> register another source in the current VM
System.memoryUsage()   -> { current, peak, arenaSize }
System.getEnv(name)    -> value or null
System.setEnv(name, value) -> 0
System.unsetEnv(name)  -> 0
System.listEnv()       -> dictionary copied into the current VM
```

Imported source and functions live only until the current VM is destroyed.
Resolve imports through the same selected filesystem.

Boots owns the environment-variable store outside every Noct arena. Use a
bounded packed store rather than one maximum-sized array per slot; start with
a 4 KiB total budget, names limited to 31 bytes, values limited to 255 bytes,
and at most 32 entries. Re-measure before increasing it. The NAPI copies names
and values at the boundary and never retains a Noct string pointer. Variables
persist across normal script and REPL sessions until Boots transfers control
to a non-returning payload or the machine resets.

Add shell built-ins `env`, `set NAME VALUE`, and `unset NAME`. BOOT.CFG uses
the same commands, so configuration and interactive state have identical
semantics. Invalid names, embedded `=`, overflow, and full-store conditions
must return an explicit error without modifying the old value.

## 14. Block API and dangerous APIs

Implement raw blocks after File/FAT16 write tests pass:

```text
Block.list()                         -> detected block descriptors
Block.getSectorSize(alias)           -> 512 initially
Block.getSectorCount(alias)          -> bounded known count or error
Block.read(alias, lba, count)        -> packed uint8 array
Block.write(alias, lba, data, count) -> sectors written
Block.sync(alias)                    -> 0
```

Initial maximum transfer is 64 sectors per call and is internally performed
as one BIOS sector per gateway operation. Validate `lba + count` overflow and
known device bounds. After a raw write to the currently mounted volume,
invalidate caches and remount before subsequent filesystem calls.

Future direct physical memory, BIOS calls, and port I/O use explicit names:

```text
Unsafe.Memory.*
Unsafe.BIOS.*
Unsafe.IO.*
```

They require a separate design and user review. Do not smuggle raw access into
the safe `System` or `Block` API.

## 15. Script utilities, REPL, and Remacs integration

### 15.1 HELLO.NCT

First target script. It prints arguments, performs integer and soft-float
arithmetic, allocates arrays/dictionaries, forces JIT with a loop, and returns
zero. It is used by host and QEMU tests.

### 15.2 LS.NCT

Uses only `Directory.list`. Output includes name, size, and directory marker.
No private FAT access.

### 15.3 CP.NCT

Uses only standard `File` API. Copy in a bounded buffer (initially 8 KiB),
check every read/write, close both files on all normal error paths, and return
nonzero on failure.

### 15.4 DD.NCT

Uses only `Block` API. Require explicit source, destination, block size/count,
and bounded transfer. Print progress periodically with carriage return. It is
not part of the first safe NAPI milestone.

### 15.5 Remacs editor integration

Do not implement `EDIT.NCT`. The separately maintained Noct program Remacs is
the editor for Boots. Treat it as an external application with its own origin,
revision, license, and update path. First inventory the generic Screen,
Keyboard, File, Directory, and environment operations Remacs actually uses.
Add only generally useful NAPI operations; never add a hidden Remacs-specific
native helper.

Acceptance covers loading Remacs from the BOOT FAT16 partition, opening,
editing, saving, reopening, and exiting to a visible Boots cursor. Keep the
Remacs source out of the Boots repository until its repository URL and license
are recorded and the user approves either a submodule, release snapshot, or
packaging-only dependency.

### 15.6 Reusable Noct REPL

Fix the REPL in upstream Noct before integrating it into Boots. Split the
current `src/cli/cli-repl.c` into a host-independent session engine and thin
Linux CLI input/signal glue. The engine receives complete lines, owns a
bounded source accumulator, reports `READY`, `NEED_MORE`, `EXECUTED`, `ERROR`,
or `EXIT`, and operates on an environment supplied by its host. It must not
call `fgets`, install signals, print prompts, or create hardware dependencies.

Replace raw brace counting with a lexical completeness scanner that respects
strings, escapes, line and block comments, and balanced `()`, `[]`, and `{}`.
Test nested functions/loops/conditionals, braces inside strings and comments,
blank lines, syntax errors, EOF during a block, and the source-size limit. A
function defined during a session remains visible to later input in that same
session.

The Linux CLI creates one VM at REPL entry, registers its normal libraries,
and destroys it on EOF or Ctrl-C. Its SIGINT handler is scoped to REPL input,
restores the previous disposition, and converts an interrupted read into an
orderly REPL exit. Interrupting an already-running infinite Noct function is
not claimed until the VM has an explicit safepoint/cancellation mechanism.

Boots adds a no-argument `noct` shell form for the REPL; `noct FILE.NCT` keeps
its existing meaning. Boots creates one VM and high-memory arena at entry,
registers the safe NAPI plus environment API, displays `> ` or `. `, and
destroys the VM, closes streams, resets the arena, and restores the cursor on
Ctrl-C (ASCII 0x03) or fatal session error. Ctrl-C is recognized while reading
input; script-execution cancellation has the same limitation as Linux.

## 16. Build and source reproducibility

### 16.1 No network during product build

`./build.sh bootloader`, image generation, and release generation use the
already-initialized, pinned `third_party/noct` submodule. Network access is
used only by explicit `noct init` and `noct update` maintainer commands.

### 16.2 Maintainer update script

Maintain `scripts/update-noct.sh` with:

```text
init                           initialize the pinned submodule
status                         show URL, pinned/current commit, dirty state
update [REF]                   fetch and stage a new gitlink
verify                         license, gitlink, clean generated state
```

The script refuses a dirty submodule, never advances during an ordinary build,
and never deletes unrelated files. Updating the gitlink follows successful
upstream Linux host tests and precedes the complete Boots regression matrix.

### 16.3 Build reports

Every BOOT.SYS build prints:

- imported Noct commit;
- SMALL or TINY profile;
- JIT maximum;
- image and BSS sizes;
- unresolved symbols before final link, which must be empty;
- opcode-check result.

`build.sh release` must run the opcode check and Noct host tests before
packaging once Noct is part of a public release.

## 17. Test strategy

### 17.1 Upstream Noct tests

Before and after each generic Noct change:

```sh
cmake --preset <normal-host-preset>
cmake --build --preset <matching-build-preset>
tests/run-syntax.sh <built-noct-cli>
```

Use the actual current preset names. Record command and result in the review
report. PC98BE changes must not regress ordinary host builds.

### 17.2 Host BOOT98 tests

Extend `make -C bootloader check` to run:

1. heap allocate/free/realloc/coalesce/OOM/reset tests;
2. libc string/format/conversion tests;
3. soft-float known-vector tests;
4. FAT16 read/write/create/truncate tests on memory-backed 512- and 1024-byte
   logical-sector images;
5. Noct create/register/run/destroy repeated at least 100 times;
6. syntax error and runtime error cleanup;
7. forced allocation failure at each initialization stage;
8. File API create/write/read/close/finalizer behavior;
9. NAPI argument/range validation;
10. script lookup precedence and `NAME` -> `NAME.NCT` resolution.

Host tests must not depend on the PC-98 BIOS. Use injected volume, console,
keyboard, and clock callbacks.

### 17.3 Static opcode checks

Add a dedicated script that disassembles all BOOT.SYS and soft-float objects.
Reject at least:

- x87 instructions;
- CMOV and other i686 instructions;
- CMPXCHG/XADD where not explicitly reviewed;
- MMX/SSE/AVX;
- compiler-generated calls to unresolved host runtime functions.

Do not rely only on text grep of source. Save the disassembly and rejected
instruction report under `build/logs/`.

JIT-generated code is verified by a corpus that forces compilation and
execution of every implemented arithmetic, branch, call, array, dictionary,
and loop path under QEMU `-cpu 386`.

### 17.4 QEMU matrix

Use only `~/linux-pc98/qemu-pc98`; do not use the old standalone QEMU tree.
Use exact-process termination and never kill other QEMU instances in bulk.

Minimum matrix:

| Machine | RAM | Profile | Required result |
|---|---:|---|---|
| `pc9801`, `-cpu 386` | 6 MiB | SMALL | HELLO.NCT returns, then BusyBox boots |
| `pc9801`, `-cpu 386` | 5 MiB | TINY | HELLO.NCT returns, then BusyBox boots |
| `pc9801`, `-cpu 386` | 5 MiB | TINY OOM test | clear error and usable BE shell |
| `pc9801`, `-cpu 386` | 8 MiB | SMALL File test | marker file written and readable |
| `pc9821`, `-cpu 486` | 64 MiB | SMALL | Noct returns, Debian boots |

For automated write validation, work on a temporary image copy without
QEMU snapshot mode, execute a script that writes a unique marker file, stop
only that QEMU process after the kernel success marker, and inspect the FAT16
image with the host test/tool. Never run a write test on a user image.

### 17.5 Existing BOOT98 regression matrix

After every integration milestone, retain:

- IDE and PC-9801-92 SCSI boot;
- primary/secondary IDE distinction;
- missing/corrupt BOOT.SYS fallback;
- BOOT.CFG automatic Linux boot;
- Escape-to-shell and cursor visibility;
- applet and IPLware return;
- chain boot to PBR;
- FAT16 fragmented read;
- Linux ELF progress display and entry.

### 17.6 Real hardware gate

After QEMU passes, test at least:

- one i386 machine without FPU, 5 or 6 MiB if available;
- one i486DX machine;
- IDE boot and one 55/92-compatible SCSI boot where available;
- repeated script return, keyboard navigation, File write, and editor save.

Do not claim 5 MiB support until real hardware or an equivalent constrained
QEMU test passes with measured free arena space.

## 18. Ordered implementation milestones

Each milestone ends with a user review. Do not commit the next milestone into
the same changeset. Do not push unless the user asks.

### M0 — Refresh baseline and dependency inventory

Changes: documentation and generated reports only.

1. Confirm repository status and latest commits.
2. Run existing BOOT98 build/check and headless BusyBox regression.
3. Run upstream Noct host syntax tests.
4. Generate a selected-source include/function/unresolved-symbol inventory.
5. Record current BOOT.SYS section sizes and memory map.

Acceptance: no source behavior changed; baseline tests and known failures are
recorded. Stop if the baseline itself is broken.

### M1 — Upstream Noct PC98BE portability

Status: **COMPLETE — REVIEWED 2026-08-07**.  See
`PC98BE-NOCT-M1-UPSTREAM.md` for the exact source and verification record.

Repository: NoctLang only.

Implement Sections 8.1, 8.2, and 8.5. Keep host targets passing. Do not add BE
hardware glue.

Acceptance: upstream host tests pass; a PC98BE object-only build compiles the
selected core with custom allocator declarations and no POSIX/DOS4G branch.

### M2 — Import Noct and establish reproducible build

Status: **IMPLEMENTED — REVIEWED AND COMMITTED**.  See
`PC98BE-NOCT-M2-IMPORT.md` for the imported revision, build boundary, and
verification record.

Historical note: M2 originally used a squashed subtree. M12 supersedes only
that transport mechanism with a git submodule; the reviewed source boundary
and build-object layout established by M2 remain valid.

Repository: linux-pc98.

1. Import approved Noct commit by subtree.
2. Add `UPSTREAM.md`, update script, `noct.mk`, and license inventory.
3. Compile selected sources to objects, but do not link into BOOT.SYS yet.

Acceptance: offline rebuild works; imported tree is unmodified; no generated
objects appear under `third_party/noct`.

### M3 — Heap and integer-only libc foundation

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-07**.  See
`PC98BE-NOCT-M3-LIBC.md` for the implementation boundary, measurements, and
verification record.

Implement heap, headers, memory/string/integer formatting, and host tests.
Temporarily compile Noct with JIT disabled and do not execute it in BOOT.SYS.

Acceptance: heap/libc host tests pass including fault injection; static
objects have no host libc references except the explicitly deferred
soft-float/math symbols.

### M4 — Minimal interpreter lifecycle

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-07**. See
`PC98BE-NOCT-M4-LIFECYCLE.md` for lifecycle, size, and QEMU evidence.

1. Link Noct core without standard APIs and with JIT disabled.
2. Add `boot98_noct_run` cleanup structure.
3. Register a minimal Console.write function.
4. Execute an embedded constant source string first.
5. Return to shell 100 times in host test and repeatedly in QEMU.

Acceptance: `main()` and a deliberate syntax/runtime error both return to a
usable shell; memory current count returns to baseline after every run.

### M5 — Soft-float completeness

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-07**. See
`PC98BE-NOCT-M5-SOFTFLOAT.md` for source provenance, numerical vectors,
opcode audit, size, and 6 MiB QEMU evidence.

Implement Section 7 and the float formatting/conversion portion of libc.
Enable Noct float/double syntax and intrinsics in the interpreter.

Acceptance: known vectors and Noct float scripts pass; no x87/i486+ opcode;
6 MiB SMALL QEMU still returns to shell cleanly.

### M6 — i386 JIT

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-07**. See
`PC98BE-NOCT-M6-JIT.md` for runtime policy, lifecycle, size, and 6 MiB i386
QEMU evidence.

Import the approved Noct JIT platform hook, enable x86 JIT, add configurable
code maximum, and force JIT execution with the corpus.

Acceptance: interpreter and forced-JIT output match; QEMU `-cpu 386` passes;
VM destruction releases JIT storage; the Noct binary and final BOOT.SYS pass
the i386 static opcode audit. JIT is mandatory for completion, not an optional
optimization. Generated JIT bytes are not linearly disassembled because the
x86 generator interleaves skipped constant data with instructions.

### M7 — NCT file loading and shell resolution

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-08**. See
`PC98BE-NOCT-M7-FILES.md` for command resolution, memory ownership, error
paths, size, host tests, and 6 MiB i386 QEMU evidence.

Load source from the current FAT16 filesystem, pass args to zero- or one-arg
`main`, implement explicit and implicit command resolution, and add HELLO.NCT.

Acceptance: built-ins retain precedence; `hello a b`, explicit `noct
HELLO.NCT a b`, missing file, oversized file, and invalid main signature all
behave as specified and return to the shell.

### M8 — Safe console/screen/keyboard/directory NAPI

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-08**. See
`PC98BE-NOCT-M8-NAPI.md` for the service-table boundary, API contract, key
namespace, ownership rules, size, host tests, and 6 MiB i386 QEMU evidence.

Implement Sections 13.1–13.5 except file writes. Add host-injected tests and a
read-only screen/key demo.

Acceptance: positional Shift-JIS output is bounded, cursor is restored after
return, special keys are stable in QEMU, and Directory lists the BOOT root.

### M9 — BIOS and generic filesystem writes

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-08**. See
`PC98BE-NOCT-M9-WRITES.md` for the service-11 contract, low-memory bounce
path, stable filesystem results, isolated test image method, size delta, and
IDE/SCSI QEMU evidence.

Implement Sections 11.1 and 11.2, then test raw sector writes only on temporary
images. Do not begin FAT metadata writes until read-back validation passes on
QEMU IDE and SCSI BIOS paths.

Acceptance: a temporary sector can be written/read/restored through the BIOS
gateway; all preexisting read and boot tests still pass.

### M10 — FAT16 writer and stdio/File API

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-08**. See
`PC98BE-NOCT-M10-FILE-API.md` for the cache and metadata-ordering rules,
stdio/File ownership contract, limitations, size delta, host tests, upstream
Noct tests, and independent QEMU marker-file evidence.

Implement Section 11.3, BE stdio, upstream-corrected `api-file.c`, and host
filesystem tests.

Acceptance: create, truncate, extend, overwrite, close/finalizer, both FAT
copies, full-disk, full-root, and injected-I/O-error cases pass. QEMU writes a
marker file and the host independently reads it.

### M11 — Noct utilities

Status: **COMPLETE — REVIEWED AND COMMITTED 2026-08-08**. See
`PC98BE-NOCT-M11-UTILITIES.md` for the public-API boundary, path guard,
bounded-copy behavior, packaging, and host/QEMU evidence.

LS.NCT and CP.NCT use only public NAPI. Bounded Block NAPI and DD.NCT remain a
separately reviewed post-safe-core feature because they expose destructive raw
writes; they do not block the first Boots release.

### M12 — Noct submodule migration and upstream refresh

Status: **IMPLEMENTED — AWAITING USER REVIEW**.

1. Replace the imported Noct subtree with a `third_party/noct` gitlink.
2. Pin commit `765df9ed88439eed91d118ab9bdbc6d442524527`.
3. Provide explicit `init`, `status`, `update`, and `verify` maintainer
   commands; no product build fetches implicitly.
4. Run the upstream Noct tests, rebuild Boots, and rerun M11 verification.

Acceptance: a recursive clone builds offline, an uninitialized or dirty
submodule fails with an actionable diagnostic, the staged gitlink equals the
checked-out Noct commit, and the existing Noct/Boots tests still pass.

### M13 — Reusable upstream REPL and Linux CLI

Repository: NoctLang first, then advance the linux-pc98 gitlink.

1. Split the existing CLI REPL into a host-independent session engine and a
   thin Linux input/signal frontend.
2. Replace brace counting with a bounded lexical scanner that ignores braces
   inside strings and comments and balances `()`, `[]`, and `{}`.
3. Give the session engine explicit READY/NEED_MORE/EXECUTED/ERROR/EXIT
   results and no dependency on stdio, POSIX signals, or terminal control.
4. On Linux, make Ctrl-C exit the REPL cleanly and restore the prior signal
   handler. Do not claim that Ctrl-C can interrupt an already-running JIT
   program until Noct has explicit safepoints.

Acceptance: Linux single- and multiline REPL tests pass, braces in strings or
comments do not confuse continuation, EOF and Ctrl-C release the VM exactly
once, and existing CLI behavior outside the REPL is unchanged.

### M14 — Boots environment variables

1. Add a bounded Boots-owned environment store outside the Noct arena.
2. Add shell/BOOT.CFG commands `env`, `set NAME VALUE`, and `unset NAME`.
3. Export `System.getEnv`, `System.setEnv`, `System.unsetEnv`, and
   `System.listEnv` through safe NAPI with copied strings only.
4. Start with a 4 KiB packed store, at most 32 entries, names up to 31 bytes,
   and values up to 255 bytes; reject overflow without changing the old value.

Acceptance: variables persist across ordinary scripts and REPL sessions until
boot/reset, no pointer into a destroyed VM survives, full-store and invalid
name cases are deterministic, and existing BOOT.CFG files remain compatible.

### M15 — Boots REPL integration

Status: **IMPLEMENTED AND VERIFIED**.

1. Keep `noct FILE.NCT [args...]` unchanged and make argument-free `noct`
   enter the interactive REPL.
2. Create one VM on entry, register the same safe NAPI as file scripts, and
   destroy/reset it on every exit path. Register the M14 environment bridge
   through this same path after M14 is implemented.
3. Use the Boots line editor and visible cursor. ASCII Ctrl-C (`0x03`) exits
   to a usable shell and restores console state.
4. Preserve top-level function declarations between successful REPL
   submissions. Ordinary `var` declarations remain evaluator-local by Noct
   language design; use M14 environment variables for persistent values.
   Syntax/runtime errors report and continue unless VM integrity is lost.

Acceptance: repeated enter/evaluate/error/Ctrl-C cycles return arena use to
baseline, multiline input works on QEMU `-cpu 386`, and file-script execution
still creates a fresh VM per invocation.

### M16 — Remacs compatibility and integration

Do not implement or ship EDIT.NCT. Remacs is an independently maintained Noct
program and supplies the editor demonstration. Inventory the generic
Screen/Keyboard/File/Directory/environment services it uses, fill only generic
NAPI gaps, and keep Remacs-specific policy in the application. Do not vendor
Remacs until its repository URL, license, pinned revision, and release
packaging mode are reviewed.

Acceptance, after Remacs is approved for import: it loads from the BOOT FAT16
partition, opens/edits/saves/reopens a file, exits with a visible Boots cursor,
and requires no editor-specific native helper.

### M17 — TINY and 5 MiB optimization

1. First make the complete Boots + Noct path work with 5 MiB of installed RAM;
   this is the non-negotiable minimum profile.
2. Measure SMALL peaks, add upstream `NOCT_MEMORY_TINY`, and reduce buffers,
   GC working sets, and JIT capacity based on data.
3. Detect installed memory once and select a runtime profile for 5, 8, 16,
   32, 64, or more than 64 MiB. Keep one code path and vary bounded arena,
   GC, source-buffer, and JIT limits rather than building incompatible Boots
   binaries.
4. Clamp safely to the next smaller profile when the detected usable region
   is fragmented or reserved; never let profile selection overlap a loaded
   kernel or Boots resident memory.
5. Run the full TINY/SMALL corpus and lifecycle tests at every boundary.

Acceptance: the required 5 MiB QEMU case passes, every boundary selects the
documented profile, larger machines receive larger useful Noct/JIT arenas,
and all exit paths return arena use to baseline. Do not silently raise the
5 MiB minimum.

### M18 — Boots naming and zlib license conversion

1. Inventory every source, header, assembly, generated, and imported file
   under `bootloader/` with provenance and current license.
2. Change only sole-author Boots-owned files to `SPDX-License-Identifier:
   Zlib`, add the Boots zlib license, and retain all Noct, GCC soft-fp, musl,
   and other third-party notices and license terms.
3. Change user-visible product text and documentation to `Boots: Boot loader
   and pre-boot environment`; retain compatibility filenames and internal
   `boot98_*`/`PC98BE` identifiers unless separately reviewed.

Acceptance: the license manifest accounts for every distributed source and
binary input, automated header checks pass, and no third-party file is
relicensed by inference.

### M19 — Documentation and release integration

Update design/readme, root README, build help, release scripts, license
inventory, and release notes. Include pinned Noct/Remacs revisions and measured
memory requirements.

Acceptance: `./build.sh release` reproduces artifacts offline after explicit
submodule initialization, runs mandatory host/opcode tests, and includes the
approved `.NCT` applications in the BOOT partition without breaking existing
image profiles.

## 19. Review checklist for every milestone

The implementing model must report:

1. exact files changed;
2. why each change belongs upstream Noct or linux-pc98;
3. commands run and pass/fail result;
4. BOOT.SYS image/BSS delta;
5. peak script-arena use if runtime code changed;
6. opcode-audit result if target code changed;
7. QEMU command line and exact image profile if QEMU was used;
8. known limitations and the next milestone only.

Never:

- commit or push without user instruction;
- edit `~/qemu-pc98`; use `~/linux-pc98/qemu-pc98` if QEMU changes become
  necessary;
- kill all QEMU processes;
- modify a user disk image in place;
- download dependencies during normal product builds;
- hide a failing test by disabling JIT, floats, warnings, or assertions;
- place generic Noct fixes only in the checked-out submodule;
- advance past a review gate merely because the next change appears small.

## 20. Principal risks and mandatory mitigations

| Risk | Detection | Mandatory response |
|---|---|---|
| Resident Noct code exceeds low-memory window | link map/assert | strip excluded features; if still too large, propose a high-memory Noct overlay before changing loader layout |
| SMALL leaves no memory on 5 MiB machine | peak counters/QEMU | implement measured TINY; keep clean OOM; do not overcommit RAM |
| Soft-float still emits x87 or newer opcodes | disassembly and `-cpu 386` | fix source/runtime; never require an FPU for i386SX profile |
| Host libc symbol leaks into BOOT.SYS | final unresolved-symbol inventory | implement or remove dependency explicitly |
| JIT corrupts allocator or survives reset | repeated forced-JIT runs | repair lifetime ownership before continuing |
| Script error leaves hidden cursor or broken shell | error corpus | centralize one cleanup/console-restore path |
| FAT write corrupts both copies | memory image plus independent checker | keep writer unreleased; fix ordering/bounds; test temporary copies only |
| BIOS write differs by controller/ROM | IDE/SCSI QEMU and hardware | keep writes disabled for unverified paths; read-only Noct remains usable |
| Standard File API semantics differ on short I/O | upstream unit tests | fix target-independent behavior upstream |
| Noct submodule is absent, dirty, or at the wrong revision | submodule verifier | fail before compilation with initialization/update instructions; never fetch implicitly |
| Noct upstream changes during integration | recorded gitlink commit | run upstream tests, advance one reviewed gitlink, never mix revisions |
| REPL multiline parser misclassifies source | lexical-session tests | recognize strings, escapes, and comments; enforce bounded input and preserve the previous usable session |
| Ctrl-C leaks a VM or leaves terminal state changed | repeated host/QEMU cycles | funnel EOF, Ctrl-C, errors, and normal exit through one session cleanup path |
| Environment storage overflows or retains VM pointers | boundary/fault tests | copy across the NAPI boundary; reject atomically; keep storage outside the script arena |
| Boots zlib conversion drops a third-party notice | complete license manifest/header check | relicense only audited sole-author files and preserve Noct/GCC/musl terms |
| Unsafe NAPI destroys the VM/BE | separate namespace and review | defer until safe APIs and recovery are stable |
| Remacs demands application-specific native helpers | API inventory/review | extend only generic Screen/Keyboard/File services; keep editor policy in Remacs |

## 21. Definition of done

The Noct BE integration is complete only when all statements are true:

- Noct source and license are reproducibly included in linux-pc98.
- `NOCT_TARGET_PC98BE` is accepted upstream.
- HELLO.NCT runs with forced JIT on `-cpu 386` without FPU instructions.
- Normal return, syntax error, runtime error, and OOM return to a usable shell.
- No per-script heap use remains after arena reset.
- Standard File reads and writes work on the BOOT FAT16 partition.
- LS.NCT and CP.NCT work through public APIs.
- Noct is pinned as a verified submodule and normal builds work offline after
  explicit initialization.
- The Linux Noct REPL has robust multiline input and exits cleanly on Ctrl-C.
- The Boots `noct` command provides a session VM that is fully destroyed on
  exit and can read/write bounded Boots environment variables.
- Remacs, after its source and packaging review, edits and saves a file using
  only public generic APIs; no in-tree EDIT.NCT is implemented.
- Existing Linux/chain/app/ IPLware boot regressions pass.
- Boots-owned files are zlib-licensed only after a complete provenance audit;
  all imported license notices remain intact.
- SMALL has a documented measured RAM minimum.
- TINY either boots and executes the required script at 5 MiB or has an
  honest documented measured limitation.
- Build and release paths work offline and opcode checks are mandatory.

Only after this definition is met should the project begin the separately
reviewed `Unsafe.*` hardware API and more general OS-like utilities.
