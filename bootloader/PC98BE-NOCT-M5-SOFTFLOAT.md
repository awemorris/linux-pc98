# PC-98 Bootstrap Environment: M5 Software Floating Point

Status: **M5 IMPLEMENTED — AWAITING USER REVIEW**

Recorded on 2026-08-07 for M5 of
`PC98BE-NOCT-IMPLEMENTATION-PLAN.md`. This milestone replaces M4's deliberate
floating-point traps with a complete software implementation for every ABI
operation and math entry point used by the selected Noct interpreter. It also
adds decimal parsing and formatting without introducing x87, MMX, SSE, or an
i486-or-newer instruction.

## 1. Baseline and managed sources

| Item | Value |
|---|---|
| linux-pc98 starting commit | `3dc65e3f0fc6` (`wip: Noct integration`) |
| Noct snapshot | `86079e47b8430a9fce4c67fab584499a3531658e` |
| GCC source revision | `a0e5e713daa14252912d420a2aaa3107fc874a80` (GCC 14.3 tree) |
| musl source revision | `78c0972e439e7473f8660655fed5c24db5a929d5` (musl 1.2.6 tree) |
| build compiler | GCC 14.2.0 (`gcc (Debian 14.2.0-19)`) |
| target ABI | ELF i386, `-msoft-float -mno-80387 -mlong-double-64` |

The build compiles the managed GCC and musl files in place. It neither copies
nor edits those upstream files. `softfloat/softfloat.mk` names every selected
translation unit explicitly, so a toolchain update cannot silently expand the
code or licensing boundary.

## 2. Implementation boundary and provenance

| Facility | Source | Notes |
|---|---|---|
| SF/DF add, subtract, multiply, divide | `toolchain/gcc/libgcc/soft-fp/{add,sub,mul,div}{sf,df}3.c` | GCC/glibc soft-fp compiler ABI |
| comparisons and SF/DF conversion | GCC soft-fp `eq*`, `ge*`, `le*`, `extendsfdf2.c`, `truncdfsf2.c` | Alias symbols supplied by the upstream sources |
| signed/unsigned 32/64-bit conversions | GCC soft-fp `fix*`, `float*` files listed in `softfloat.mk` | Includes all unresolved Noct ABI calls |
| `sinf`, `cosf`, `tanf`, `sqrtf` | selected musl 1.2.6 math sources and their explicit helpers | These are the Math intrinsics used by Noct |
| `strtod`, `strtof`, `strtold`, `atof` | musl `shgetc.c`, `floatscan.c`, and `strtod.c` | Private string-only FILE adapter; no product stdio ABI change |
| `%.7g`, `%.15g`, `%f`, `%e` | `libc/boot98-format.c` | Original PC98BE bounded formatter |
| musl compatibility shims | `softfloat/boot98-musl-compat.c` and private headers | Original PC98BE target adaptation |

The GCC soft-fp source headers retain their LGPL-plus-linking-permission
notices. The managed GCC license text is in `toolchain/gcc/COPYING.LIB`.
Selected musl files retain their individual BSD/Sun or musl notices, and the
aggregate notice is in `toolchain/musl/COPYRIGHT`. Noct remains under its
zlib license in `third_party/noct/LICENSE`. `build-bootloader-dist.sh` now
places all three notices in `bootloader.zip`.

## 3. ABI and libc details

GCC emits helper calls because all Noct, libc, musl, and PC98BE adapter objects
use the same i386 software-float flags. The final ELF has no unresolved
symbol. Long double is deliberately compiled as 64-bit for the imported musl
scanner; this avoids an 80-bit x87 ABI while retaining the exact binary64
range needed by Noct Double.

The musl scanner sees a private, read-only FILE-shaped object backed by a
NUL-terminated string. Only the `shgetc` operations needed by `floatscan` are
provided. This is not exposed as PC98BE `stdio.h` and cannot perform host or
BIOS I/O. `atof` delegates to the same `strtod`, so source literals and the
Noct conversion intrinsics share one conversion path.

The PC98BE formatter classifies sign, negative zero, infinity, and NaN by
their binary64 representation. It supports fixed, exponential, and general
formats, C99 `snprintf` length/NUL semantics, precision up to 17 significant
digits, width, padding, sign, alternate form, and upper-case variants. Noct's
required `%.7g` Float and `%.15g` Double output are exact test vectors.

## 4. Host numerical verification

Primary command:

```sh
./build.sh noct verify
```

The isolated `boot98-softfloat-host-test` checks exact IEEE-754 bit patterns
for SF and DF arithmetic, signed and unsigned integer conversions, decimal
and hexadecimal parsing, zero, negative values, infinity, NaN, overflow,
underflow, `sqrtf`, `sinf`, `cosf`, and `tanf`. Formatting checks include:

```text
0.3831776
0.383177570093458
1e+20
1.500000
1.23e+02
-0
```

The Noct lifecycle test then executes Float and Double division plus all four
Math intrinsics, compares the exact console output, and retains the existing
100-cycle normal/error cleanup checks. Result: **PASS**.

## 5. Static instruction and link audit

Every selected soft-fp/math/scanner object and the final `boot98-stage2.elf`
is disassembled. The audit rejects x87, MMX, SSE/AVX, CMOV, CMPXCHG, XADD,
CPUID, RDTSC, and the other post-i386 patterns defined in the plan. The saved
artifacts are:

```text
build/logs/boot98-m5.disassembly
build/logs/boot98-m5-rejected.txt
```

The rejected-instruction file is empty and the final ELF has no undefined
symbols. Result: **PASS**.

## 6. Six MiB QEMU verification

`tests/BOOT-M5-QEMU.CFG` runs the embedded program three times. A disposable
image was generated from the canonical i386SX IDE release image with the
normal image builder, then booted using only the in-tree QEMU:

```text
qemu-pc98/build/qemu-system-i386
  -M pc9801 -cpu 386 -m 6 -accel tcg
  -L qemu-pc98/pc-bios -nic none
  -drive if=ide,bus=0,unit=0,format=raw,
         file=build/tests/noct-m5-qemu.raw,snapshot=on
  -display none -serial none -no-reboot
```

Observed terminal result:

```text
Noct M5 float: 0.3831776 double: 0.383177570093458 sqrt: 3 sin: 0.4794255 cos: 0.8775826 tan: 0.5463025
Noct M5 float: 0.3831776 double: 0.383177570093458 sqrt: 3 sin: 0.4794255 cos: 0.8775826 tan: 0.5463025
Noct M5 float: 0.3831776 double: 0.383177570093458 sqrt: 3 sin: 0.4794255 cos: 0.8775826 tan: 0.5463025
Noct M5 PASS: runs=3 peak=2643672 bytes
PC98BE-M5-QEMU-PASS
```

The script returned to the BE command stream before `halt`. The run used TCG,
not KVM, and QEMU was stopped through its own monitor without signalling any
other process. Result: **PASS**.

## 7. Size results

| Measurement | Value |
|---|---:|
| `BOOT.SYS` file/load-image bytes | 187,888 |
| change from recorded M4 | +31,904 bytes |
| resident `.text` | 181,441 bytes |
| resident `.data` | 6,416 bytes |
| resident BSS | 30,876 bytes |
| `__image_end` | `0x4ddf0` |
| bytes remaining before `0x60000` | 74,256 |
| `__bss_end` | `0x5569c` |
| bytes remaining before `0x70000` | 108,900 |
| QEMU script heap peak | 2,643,672 bytes |
| SHA-256 of `BOOT.SYS` | `2b1bd59af5d2c67bc36ba21d43fdb981c478ae96c261ed0ecb5d590d6dcf5364` |

The 256 KiB BOOT.SYS load window and low-memory BSS ceiling remain unchanged.

## 8. Review boundary and next milestone

M5 does not load `.NCT` files and does not enable the JIT. Double-precision
arithmetic, conversions, and formatting are complete for the selected Noct
core; Noct's current transcendental Math API is Float-based, so unreferenced
double `sin/cos/tan/sqrt` functions were intentionally not imported.

After review and commit, the next milestone is **M6 — i386 JIT**. M6 must not
change the established soft-float ABI and must apply the same forbidden-opcode
audit to generated code as well as static objects.
