# PC-98 Bootstrap Environment: M1 Noct Upstream Portability

Status: **M1 IMPLEMENTED — AWAITING USER REVIEW**

Recorded on 2026-08-07 for M1 of
`PC98BE-NOCT-IMPLEMENTATION-PLAN.md`.  The implementation is an uncommitted
change in the separate `~/NoctLang` repository, based on upstream commit
`40670781403c760a0793cfb324b2b53a4c2f0228` (`Add api-thread.c (wip)`).
No M2 import or BOOT.SYS link change has been made.

## 1. Implemented portability surface

| Area | Implementation |
|---|---|
| Target selection | Added `NOCT_TARGET_PC98BE`; the target remains i386 little-endian but is neither POSIX, Linux, Windows, nor DOS4G. |
| Build mode | Added an object-only `pc98be-i386` CMake preset and freestanding i386 toolchain. |
| Object model | Enforced the single-threaded object model for PC98BE. |
| Memory profile | Starts with `NOCT_MEMORY_SMALL`. |
| JIT | Kept enabled. PC98BE uses the supplied allocator and a flat executable address space, without `mmap`, DPMI, or page-protection calls. |
| Standard APIs | `System`, `Console`, and `File` source groups are now independently selectable. They remain disabled in the initial PC98BE core preset. |
| Allocation | Added the missing `noct_realloc` hook and target declarations for all five PC98BE allocation functions. Converted selected raw allocation/free sites to the hooks. |
| Generated parser | Added both `YYMALLOC` and `YYFREE` mappings in `parser.y` and the checked-in `parser.tab.c`. |
| NAPI documentation | Updated VM construction/destruction, value tags, parameter widths, allocator hooks, and the embedding example to match the public header. |

The PC98BE JIT allocation/protection branch is a small intentional extension
beyond the headings listed for M1.  It is required for the M1 acceptance rule
that the JIT-enabled object build must not fall into the POSIX or DOS4G path.
Runtime JIT integration and execution remain M6 work.

## 2. Toolchain policy

`cmake/toolchains/pc98be-i386.cmake` uses GCC with:

```text
-m32 -march=i386 -Os -ffreestanding
-fno-pic -fno-pie -fno-stack-protector
-fno-asynchronous-unwind-tables -fno-unwind-tables
-fno-isolate-erroneous-paths-dereference
-msoft-float -mno-80387 -mno-fp-ret-in-387
-mno-mmx -mno-sse -mno-sse2
```

The erroneous-path isolation option prevents GCC 14 from placing the
post-i386 `UD2` encoding on compiler-proven undefined paths.  An object-code
scan also checks for `bswap`, `cmpxchg`, `xadd`, conditional moves, `rdtsc`,
`cpuid`, FXSAVE-family instructions, and SSE/XMM use.

The build host required `libc6-dev-i386` for this object-only compile.  That
host package was installed for M1 verification.  It is not the final BE C
runtime: M2 imports the sources and M3 supplies the freestanding CRT headers
and implementations used when linking BOOT.SYS.

## 3. Verification

### 3.1 PC98BE object build

```sh
cd ~/NoctLang
cmake --fresh --preset pc98be-i386
cmake --build --preset pc98be-i386 -j 8
```

Result: **PASS**.  All 13 selected core translation units compiled.  Every
output is an `ELF 32-bit LSB relocatable, Intel i386` object.  The combined
GNU `size --totals` result was:

```text
   text    data     bss     dec     hex
 128342    6380   20172  154894   25d0e
```

This is an object sum, not the final linked BOOT.SYS size.  Debug information
and duplicate inter-object references do not contribute to the final load
image in the same way.

The target macro audit reported only:

```text
#define NOCT_TARGET_PC98BE 1
```

The allocator-symbol audit found `noct_pc98be_malloc`, `calloc`, `strdup`,
and `free` plus AST arena wrappers.  It found no direct `malloc`, `calloc`,
`realloc`, `strdup`, `free`, `mmap`, `mprotect`, or `munmap` dependency in
the selected objects.  `noct_pc98be_realloc` is declared for completeness;
the current selected core does not call it directly.

The post-i386 instruction scan was empty after adding the GCC isolation
option.

### 3.2 Host build and syntax regression

A release host build enabled the CLI, JIT, built-in i18n, all three standard
APIs, optional compiler backends, and REPL.  Result: **PASS**.

All syntax cases then matched their golden output:

| Mode | Result |
|---|---:|
| Interpreter (`--disable-jit`) | 40 / 40 pass |
| JIT (`--force-jit`) | 40 / 40 pass |

### 3.3 API component selection

A separate host build enabled only `NOCT_ENABLE_API_CONSOLE`, with System and
File disabled.  `libnoctapi.a` and `libnoct.a` both built successfully.  This
confirms the new API source switches are independent while their host
defaults preserve the previous all-API build.

### 3.4 Source hygiene

`git diff --check` passes in `~/NoctLang`.  Generated build output is not part
of the proposed change.  Neither repository has been committed or pushed by
M1.

## 4. Review boundary

Review and commit the `~/NoctLang` change separately from the documentation
change in `~/linux-pc98`.  M2 must not begin until this milestone is accepted.
The next milestone will import an approved Noct revision into linux-pc98 and
establish an offline, reproducible selected-source build, without linking it
into BOOT.SYS yet.
