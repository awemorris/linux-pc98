# PC98BE M11: Safe Noct Utilities

M11 adds the first user-facing Bootstrap Environment utilities written in
Noct: `LS.NCT` and `CP.NCT`. They use only the public safe NAPI and standard
Noct File API completed in M8 and M10. No raw sector interface or privileged
hardware access is introduced.

## Commands

`ls [PATH]` calls `Directory.list`, prints one entry per line, appends `/` to
directory names, and prints each entry's byte size. With no argument it lists
the selected BOOT filesystem root. More than one argument produces a usage
message and status 2.

`cp SOURCE DESTINATION` obtains the source size with `FileUtil`, then copies
through `File.open`, `File.read`, and `File.write` in chunks of at most 8192
bytes. It reports short reads and writes, closes both streams on handled error
paths, and prints the final byte count after success. Opening the destination
with `wb` creates or truncates it through the M10 FAT16 writer.

The copy command rejects a source and destination that differ only by ASCII
case or by one leading `/`. This protects FAT16's case-insensitive root from
truncating the source before it can be copied. The comparison is implemented
in Noct with public `String` intrinsics; it does not add a native helper.

## Deliberate limits

- The current filesystem driver supports FAT16 root-directory 8.3 files.
- `ls` does not recursively walk directories.
- `cp` does not preserve timestamps or attributes and does not copy trees.
- remove, rename, raw Block NAPI, and `DD.NCT` remain deferred.
- A bounded Block API requires a separate review of range and destructive-write
  guards before it can be exposed to scripts.

## Image and release integration

`install-boot98-image.sh` installs `LS.NCT` and `CP.NCT` beside the existing
BOOT files when the scripts are present. `build-bootloader-dist.sh` includes
both scripts in the bootloader archive. The ordinary shell resolver therefore
allows `ls` and `cp` to be invoked by their unqualified body names.

## Verification evidence

The following completed successfully on 2026-08-08:

| Check | Result |
| --- | --- |
| `ls` exact root listing and usage error | PASS |
| 16,417-byte `cp` across three chunks | PASS |
| destination byte-for-byte equality | PASS |
| case-insensitive/leading-slash same-file rejection | PASS |
| missing-source error | PASS |
| 20 repeated JIT runs of each utility with zero arena/heap residue | PASS |
| M1-M10 host and IDE/SCSI QEMU regressions | PASS |
| QEMU BOOT script runs `ls`, copies a deterministic file, then halts | PASS |
| copied QEMU image file independently extracted and compared by host | PASS |
| static Noct/libc/soft-float/final-BOOT.SYS i386 opcode audits | PASS |

Reproduce the aggregate checks with:

```sh
make -C bootloader noct-m11-verify -j32
```

The QEMU test always copies the canonical release image to
`build/tests/boot98-m11-utilities/m11-ide.raw`; it never modifies the source
image. Its exact helper is:

```sh
./scripts/test-boot98-noct-utilities.sh
```

## Size impact

M11 adds scripts, tests, and packaging logic but no new resident BOOT.SYS code.

| Region | M10 | M11 | Delta |
| --- | ---: | ---: | ---: |
| `BOOT.SYS` load image | 238,340 | 238,340 | 0 bytes |
| resident `.text` | 229,425 | 229,425 | 0 bytes |
| resident `.data` | 8,900 | 8,900 | 0 bytes |
| resident BSS | 31,504 | 31,504 | 0 bytes |

The M11 `BOOT.SYS` SHA-256 remains
`34bac5b30d47a55082df028c6a10f867b89fdfd866ea8d11ea00aaf7fea41135`.

## Files changed

- `LS.NCT`, `CP.NCT`: safe public-API utilities.
- `tests/boot98-noct-host-test.c`: real-source, repeated JIT, error, data
  integrity, and memory-lifetime tests.
- `scripts/test-boot98-noct-utilities.sh`: disposable-image QEMU test and host
  copy verification.
- `scripts/install-boot98-image.sh`: script installation into built images.
- `scripts/build-bootloader-dist.sh`: archive inclusion.
- `Makefile`: M11 test wiring and aggregate target.

The generated-JIT raw disassembly gate was also removed from the Makefile at
the user's direction. The static Noct and final BOOT.SYS opcode audits remain
mandatory; forced JIT behavior and lifetime remain covered on host and QEMU.
