# PC-98 Bootstrap Environment: M4 Minimal Noct Lifecycle

Status: **M4 IMPLEMENTED — AWAITING USER REVIEW**

Recorded on 2026-08-07 for M4 of
`PC98BE-NOCT-IMPLEMENTATION-PLAN.md`. This milestone links the JIT-disabled
Noct interpreter into `BOOT.SYS`, registers the first BE-owned Native API,
and proves that normal and error exits reclaim the per-invocation arena and
return control to the resident bootstrap shell.

## 1. Baseline and source ownership

| Item | Value |
|---|---|
| linux-pc98 starting commit | `abf217ba7` (`wip: Noct integration`) |
| Noct snapshot | `86079e47b8430a9fce4c67fab584499a3531658e` |
| C compiler | GCC 14.2.0 (`gcc (Debian 14.2.0-19)`) |
| Target ABI | ELF i386, soft-float, no x87/MMX/SSE |

The Noct subtree is unchanged. All lifecycle, target-adapter, test, and
temporary floating-point guard files added by M4 are original linux-pc98
PC98BE code with `Copyright (C) 2026 Awe Morris` and
`GPL-2.0-or-later` identifiers.

## 2. Product boundary

M4 adds the following resident path:

```text
BOOT.SYS shell command `noct-test [1..100]`
  -> expose and bound the physical arena below 15 MiB
  -> boot98_noct_run()
       -> initialize the bounded heap
       -> create one Noct VM with JIT disabled
       -> register Console.write(text)
       -> compile an embedded constant source
       -> invoke main()
       -> destroy the VM
       -> record allocator state
       -> reset the whole script arena in O(1)
  -> restore the hardware cursor and return to the shell
```

The generic lifecycle accepts an injected arena and writer callback, so the
same target objects are exercised by the host test without emulating PC-98
hardware. It rejects reentrant invocation and has one cleanup exit after VM
creation. A failed or partially completed `noct_create_vm()` is not passed to
`noct_destroy_vm()`; the arena reset safely discards its partial allocations.

The target adapter derives the upper RAM address from BIOS work area byte
`0:0401h`, clamps it below physical 15 MiB, and leaves a 64 KiB guard. The
arena starts at 1 MiB. Kernel loading is still non-returning and may overwrite
that memory only after a Noct invocation has returned and reset it.

## 3. Native API and errors

`Console.write(text)` is registered as a `Console` dictionary member using
the public Noct API. Its argument must be a Noct string. The callback writes
the string bytes without adding a newline; scripts control terminal layout.

The lifecycle distinguishes VM creation, API registration, source parsing,
runtime, and cleanup failures. Source and runtime failures report Noct's file,
line, and message through the injected writer, then follow the same destroy
and arena-reset path as successful `main()` return.

The imported snapshot's `rt_gc_cleanup()` is currently a no-op, so
`noct_destroy_vm()` does not individually free all GC regions. This does not
leak across BE commands: M4 records `bytes_before_reset`, then releases the
complete bounded arena in O(1). The host test requires current usage to be
zero, allocator validation to pass, and the allocator error count to remain
zero after every invocation.

## 4. Deferred floating point

M4 does not claim float support. The 34 compiler/math symbols identified in
M3 are resolved by explicit i386 assembly guards that all enter a fatal
`floating point requires M5 soft-float` handler. This makes an accidental
float path visible and prevents a silent host-libm or x87 dependency. The
embedded M4 program and both lifecycle error tests are integer/string only.
M5 replaces these guards with the reviewed soft-float implementation.

## 5. Host verification

Primary command:

```sh
./build.sh noct verify
```

It retains every M3 snapshot, allocator, static-link, and opcode check, then:

1. creates, runs, destroys, and resets the VM 100 consecutive times;
2. verifies exact output from `Console.write` on every successful run;
3. submits a deliberate parser error and checks the source-error path;
4. passes an integer to `Console.write` and checks the runtime-error path;
5. requires zero current heap bytes, zero allocator errors, and a valid free
   list after every arena reset;
6. links the complete `BOOT.SYS` and rejects post-i386 instructions in the
   M4 glue.

Result on the recorded host: **PASS**.

## 6. QEMU verification

The tracked `tests/BOOT-M4-QEMU.CFG` invokes `noct-test 3`, prints a final
marker, and halts. A disposable image was generated from the canonical i386
IDE image with the normal image command, so the test used the same
`IO.SYS`/`BOOT.SYS` installation path as a release image.

QEMU configuration:

```text
qemu-pc98/build/qemu-system-i386
  -M pc9801 -cpu 386 -m 8 -accel tcg
  -L qemu-pc98/pc-bios -nic none
  -drive if=ide,bus=0,unit=0,format=raw,
         file=build/tests/noct-m4-qemu.raw,snapshot=on
  -display none -serial none -no-reboot
```

Observed terminal result:

```text
Noct M4 main returned.
Noct M4 main returned.
Noct M4 main returned.
Noct M4 PASS: runs=3 peak=2643045 bytes
PC98BE-M4-QEMU-PASS
```

The run used TCG, not KVM, and QEMU was terminated through its own monitor.
No unrelated QEMU process was signalled.

## 7. Size and link results

| Measurement | Value |
|---|---:|
| `BOOT.SYS` file/load-image bytes | 155,984 |
| resident `.text` | 149,537 bytes |
| resident `.data` | 6,416 bytes |
| resident BSS | 30,876 bytes |
| `__image_end` | `0x46150` |
| bytes remaining before `0x60000` | 106,160 |
| `__bss_end` | `0x4d9fc` |
| bytes remaining before `0x70000` | 140,804 |
| QEMU script heap peak | 2,643,045 bytes |
| SHA-256 of `BOOT.SYS` | `df470b7fdb37f333674d0bedb29b1256a93011a46974f8ca2bdca8d8aa4da48d` |

The final ELF has no undefined symbols. The existing 256 KiB BOOT.SYS load
window and low-memory BSS ceiling remain unchanged.

## 8. Files changed

Core and target integration:

- `boot98-noct.[ch]`
- `boot98-noct-platform.[ch]`
- `boot98-noct-softfloat-stubs.S`
- `boot98-stage2.c`
- `Makefile`
- `noct.mk`
- `build.sh`

Tests and documentation:

- `tests/boot98-noct-host-test.c`
- `tests/BOOT-M4-QEMU.CFG`
- `README.md`
- `PC98BE-NOCT-IMPLEMENTATION-PLAN.md`
- `PC98BE-NOCT-M3-LIBC.md`
- `PC98BE-NOCT-M4-LIFECYCLE.md`

## 9. Next milestone boundary

M4 deliberately does not load `.NCT` files, expose filesystem APIs, or enable
the JIT. The next milestone only is **M5 — Soft-float completeness**. M5 must
replace every temporary float guard, add float formatting/conversion support,
run known numerical vectors and Noct float scripts, and preserve the i386SX
opcode boundary before M6 enables the JIT.
