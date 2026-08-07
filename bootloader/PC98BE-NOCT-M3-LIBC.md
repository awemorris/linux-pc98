# PC-98 Bootstrap Environment: M3 Heap and Freestanding Libc

Status: **M3 COMPLETE — REVIEWED AND COMMITTED 2026-08-07**

Recorded on 2026-08-07 for M3 of
`PC98BE-NOCT-IMPLEMENTATION-PLAN.md`.  This milestone establishes the bounded
heap and integer-only freestanding C library required by Noct.  It performs a
relocatable static-link audit, but deliberately does not link or execute Noct
in `BOOT.SYS`.  The Noct JIT is disabled throughout M3.

## 1. Baseline and source ownership

| Item | Value |
|---|---|
| linux-pc98 starting commit | `cf8375e81d6d07586daf0e1a4d04fa4fbb759447` |
| Noct snapshot | `86079e47b8430a9fce4c67fab584499a3531658e` |
| C compiler | GCC 14.2.0 (`gcc (Debian 14.2.0-19)`) |
| Target ABI | ELF i386, soft-float, no x87/MMX/SSE |

All files under `bootloader/libc/` and the M3 host test are original
linux-pc98 PC98BE code and carry `Copyright (C) 2026 Awe Morris` with
`GPL-2.0-or-later`.  The vendored Noct snapshot remains byte-for-byte
unchanged and retains its zlib license and upstream authorship.

## 2. Files changed

Build integration:

- `build.sh`
- `bootloader/Makefile`
- `bootloader/noct.mk`
- `bootloader/libc/libc.mk`
- `bootloader/libc/deferred-symbols.regex`

Allocator and runtime implementation:

- `bootloader/libc/boot98-heap.[ch]`
- `bootloader/libc/boot98-string.c`
- `bootloader/libc/boot98-ctype.c`
- `bootloader/libc/boot98-int64.c`
- `bootloader/libc/boot98-strto.c`
- `bootloader/libc/boot98-format.c`
- `bootloader/libc/boot98-stdio.c`

Freestanding headers:

- `bootloader/libc/include/{assert,ctype,errno,inttypes,limits,math}.h`
- `bootloader/libc/include/{stdarg,stdbool,stddef,stdint,stdio,stdlib}.h`
- `bootloader/libc/include/{string,time,unistd}.h`
- `bootloader/libc/include/sys/types.h`

Tests and documentation:

- `bootloader/tests/boot98-libc-host-test.c`
- `bootloader/tests/.gitignore`
- `bootloader/README.md`
- `bootloader/PC98BE-NOCT-IMPLEMENTATION-PLAN.md`
- `bootloader/PC98BE-NOCT-M3-LIBC.md`

## 3. Heap design

The allocator uses an 8-byte-aligned physical block list plus a free list.
It splits oversized blocks, coalesces adjacent free blocks in both directions,
and never recurses.  Size additions, alignment, multiplication, and arena-end
calculations are checked for overflow.

It provides current/peak byte counters, largest-free-block inspection, invalid
and double-free counting, deterministic allocation-failure injection, and an
O(1) whole-arena logical reset.  The debug validator checks physical
contiguity, allocation sizes, coalescing, free-list cycles/backlinks, and
one-to-one membership of physical free blocks.

Both the `boot98_*` allocator API and real standard C allocation symbols are
provided.  Real symbols avoid colliding with Flex's intentional local
`malloc`/`realloc`/`free` redirection in the generated Noct lexer.

## 4. Libc boundary

M3 implements the memory/string and ctype functions selected Noct references,
integer `strto*` conversion, 64-bit compiler helper operations, C99-style
integer `snprintf`/`vsnprintf`, minimal console stdio, assertions, and small
`access`, `isatty`, `fileno`, and `time` placeholders.

The custom headers are selected with `-nostdinc`; target objects therefore
cannot accidentally use host libc headers.  Filesystem-backed stdio is still
a stub, because real FAT16 file handles belong to later NAPI milestones.
Floating conversion and formatting are also deliberately incomplete in M3:
float formatting emits an explicit `<soft-float-pending>` marker and the
compiler/math symbols are deferred to M5 rather than silently using x87.

## 5. Verification

Primary command:

```sh
./build.sh noct verify
```

It performs all of the following:

1. verifies the vendored Noct origin, revision, and snapshot hash;
2. runs the 32-bit host allocator/libc test with `-Werror`;
3. exercises fragmentation/coalescing, realloc preservation, overflow,
   invalid/double free, O(1) reset, and allocation-failure injection;
4. disassembles Noct and libc objects and rejects x87 and post-i386 opcodes;
5. combines all selected Noct/libc objects with `ld -r`;
6. rejects every undefined symbol not explicitly listed as deferred
   soft-float or math work.

Result on the recorded host: **PASS**.  Existing FAT16 host tests also pass:

```sh
make -C bootloader check
```

## 6. Static-link and size results

| Artifact | text | data | bss | total |
|---|---:|---:|---:|---:|
| Freestanding libc objects | 7,494 | 20 | 92 | 7,606 bytes |
| Combined Noct + libc M3 relocatable object | 134,121 | 6,416 | 20,316 | 160,853 bytes |

The combined object has 34 undefined symbols.  Every one is in the explicit
M5 soft-float/math allowlist (`__add*`, `__sub*`, `__mul*`, `__div*`,
comparisons/conversions, `sin[f]`, `cos[f]`, `tan[f]`, and `sqrt[f]`).  There
are no unresolved host allocation, string, stdio, time, or integer-runtime
references.

## 7. BOOT.SYS regression boundary

`BOOT.SYS` was not relinked with Noct or the new libc.  Before and after M3:

| Measurement | Value |
|---|---:|
| BOOT.SYS file bytes | 14,028 |
| resident `.text/.rodata/.data` | 14,022 bytes (14,010 text + 12 data) |
| resident BSS | 10,560 bytes |
| SHA-256 | `b4ea7d4e43b1442eb48cbb423454f25606ff275d49b68aadc0dabc4caa2dd81b` |

No QEMU command was run: M3 produces no executable target-code change and the
review gate explicitly keeps Noct out of `BOOT.SYS`.

## 8. Known limitations and next milestone

- Noct remains unexecuted and its JIT remains disabled.
- Assertions in the standalone libc halt through a weak panic hook; M4 must
  connect fatal VM status to the shell lifecycle where recovery is safe.
- Filesystem stdio/NAPI is not implemented.
- Soft-float and float formatting are intentionally deferred to M5.
- The approved upstream snapshot still emits three documented diagnostics
  downgraded from `-Werror`; the imported source itself is not modified here.

The next milestone only is **M4 — Minimal interpreter lifecycle**: link the
interpreter without standard APIs or JIT, add `boot98_noct_run`, register
minimal `Console.write`, execute embedded source, and prove repeated cleanup.
