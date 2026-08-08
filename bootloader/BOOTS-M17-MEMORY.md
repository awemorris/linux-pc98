# Boots M17: adaptive Noct memory profiles

M17 makes the Boots Noct VM fit the minimum 5 MiB PC-98 configuration while
using additional memory on larger machines. The policy is selected from the
PC-98 BIOS work area before each VM is created; a script VM or REPL still owns
one arena and the whole arena is discarded at exit.

## Profiles

| Installed RAM | Arena cap | Source | REPL | Nursery / graduate / tenure | JIT code |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 5 MiB | 4 MiB | 256 KiB | 8 KiB | 128 / 32 / 512 KiB | 96 KiB |
| 8 MiB | 8 MiB | 256 KiB | 16 KiB | 256 / 64 / 1024 KiB | 192 KiB |
| 16 MiB | 14 MiB | 256 KiB | 32 KiB | 512 / 128 / 2048 KiB | 256 KiB |
| 32 MiB | 16 MiB | 256 KiB | 32 KiB | 1024 / 256 / 4096 KiB | 512 KiB |
| 64 MiB | 48 MiB | 256 KiB | 32 KiB | 2048 / 256 / 8192 KiB | 1 MiB |
| over 64 MiB | 64 MiB | 256 KiB | 32 KiB | 2048 / 512 / 16384 KiB | 2 MiB |

The actual arena is capped by the largest contiguous region. A 64 KiB guard
is left at its end. Systems with memory above the PC-98 15--16 MiB hole use an
arena beginning at physical 16 MiB; small systems use the region above 1 MiB.
The file limit is 256 KiB even in the 5 MiB profile so that the bytecode-only
`CMD/REMACS.NB` application can be loaded; the buffer is allocated only while
the application is running and does not enlarge the fixed GC spaces.

NoctLang commit `6fac6c5b07ee7aa1775b662c1f76c5f6ab7f7a8b` adds the
per-VM JIT reservation. Every architecture keeps a compile-time upper bound,
while `NoctConfig.jit_code_size` selects the amount mapped for one VM.

## BOOT.SYS low-memory map

Noct's required JIS X 0208 File API table increased `BOOT.SYS` to 278,252
bytes. The former 256 KiB window was therefore replaced by a 320 KiB window:

| Address | Use |
| ---: | --- |
| `0x20000`--`0x6ffff` | BOOT.SYS load image (maximum 320 KiB) |
| below `0x80000` | BOOT.SYS BSS |
| `0x80000` | Linux `boot_params` passed in ESI |
| `0x81000` | Linux command line |
| `0x82000` | PC-98 setup-data nodes |

Stage 1 validates the same 320 KiB limit. The kernel handoff is unchanged
apart from the ESI value; `boot_params` was never required to reside at a
fixed physical address.

## Verification

The following command passed on 2026-08-08:

```sh
make -C bootloader noct-m17-verify -j32
```

It includes host allocator/GC/JIT lifecycle tests, the complete prior
M4--M15 suite, IDE and SCSI BIOS write/read/restore checks, the i386 opcode
audit, and QEMU REPL tests at 5, 8, 16, 32, 64, and 96 MiB. Each QEMU run
verified multiline input, error recovery, Ctrl-C exit, script resumption, and
destruction of the VM arena.
