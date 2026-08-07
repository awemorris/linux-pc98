# PC98BE M9: BIOS and Generic Filesystem Writes

M9 adds the reviewed write boundary below the future Noct `File` API.  It does
not yet modify FAT metadata and it does not expose raw writes to the normal BE
shell or to Noct.  FAT16 therefore remains read-only until M10.

## Generic filesystem contract

`boot98-fs.h` now defines the stable `enum boot98_fs_result` values needed to
distinguish success, not-found, invalid-path, read-only, no-space, I/O,
corruption, unsupported-operation, and invalid-argument results.  Filesystem
driver callbacks use this result type.  Existing BOOT.SYS callers retain the
old Boolean `boot98_fs_mount`, `boot98_fs_open`, `boot98_file_read`,
`boot98_fs_readdir`, and `boot98_file_contiguous_lba` wrappers.

A volume may supply this optional physical-sector callback:

```c
typedef int (*boot98_volume_write_t)(void *context, uint32_t absolute_lba,
                                     const void *sector);
```

The generic write helper accepts exactly one 512-byte physical sector,
checks relative-to-absolute LBA overflow, reports a missing callback as
`BOOT98_FS_READ_ONLY`, and maps callback failure to `BOOT98_FS_IO_ERROR`.
The driver interface has optional create, write, truncate, flush, and stat
operations ready for M10.  The FAT16 driver's omitted write operations are
intentionally reported as read-only.

## BIOS gateway service 11

`BOOT98_BIOS_DISK_WRITE` is appended as service 11; no existing ABI number or
request layout changed.  Its protected-mode entry performs these checks
before leaving 32-bit mode:

1. the request is a non-null, real-mode-addressable 16-byte object;
2. the BIOS ID denotes IDE `80h`-`83h` or SCSI `A0h`-`A7h`;
3. heads and sectors are nonzero;
4. LBA-to-CHS conversion produces a cylinder no greater than `ffffh`;
5. the caller buffer is non-null and its 512-byte range does not wrap.

It then copies exactly 512 bytes from caller memory to physical `00008000h`.
The real-mode half converts the LBA with that device's BIOS logical H/S and
issues PC-98 fixed-disk `INT 1Bh/AH=05h`, matching the independently used
implementation in `dos/inst.c`.  High memory is never passed to firmware.
The existing read service still uses `AH=06h` and copies its bounce sector in
the opposite direction after protected mode is restored.

## Destructive test isolation

The ordinary `BOOT.SYS` contains no raw-sector test command.  A separate,
git-ignored `BOOT-M9.SYS` build enables `m9-write-test LBA`.  That command:

1. saves the original sector through the BIOS read gateway;
2. writes a deterministic test pattern;
3. reads and compares the pattern;
4. restores the original sector on every path after a successful write;
5. reads and compares the restored data.

`scripts/test-boot98-bios-write.sh` always uses a private copy below
`build/tests/`, extends it by at least one complete BIOS cylinder, and chooses
the first added sector.  It also compares that sector on the host before and
after QEMU.  Release images are never opened writable.  The test-only command
reports through QEMU debug port `e9h`; this code is absent from normal builds.

`BOOT98_STAGE2_IMAGE` lets `install-boot98-image.sh` install such an explicitly
selected test stage while keeping normal `BOOT.SYS` as the default.

## Verification evidence

The following completed successfully on 2026-08-08:

| Check | Result |
| --- | --- |
| Generic volume write/read/restore host test | PASS |
| Missing write callback | `BOOT98_FS_READ_ONLY` |
| Injected callback failure | `BOOT98_FS_IO_ERROR` |
| Relative-LBA overflow and bad sector size | `BOOT98_FS_INVALID_ARGUMENT` |
| Existing 512/1024-byte FAT16 host tests | PASS |
| Full libc, soft-float, Noct, and M8 regression targets | PASS |
| QEMU IDE BIOS H=8/S=17, added LBA 74120 | write/read/restore PASS |
| QEMU PC-9801-92 SCSI BIOS H=8/S=32, added LBA 73984 | write/read/restore PASS |
| Host comparison after each QEMU run | original 512 bytes restored exactly |

The reproducible aggregate target is:

```sh
make -C bootloader noct-m9-verify
```

## Size impact

| Region | M8 | M9 normal build | Delta |
| --- | ---: | ---: | ---: |
| `IO.SYS` | 2,864 bytes | 3,216 bytes | +352 bytes |
| `BOOT.SYS` load image | 225,648 bytes | 226,832 bytes | +1,184 bytes |
| resident `.text` | 218,518 bytes | 219,691 bytes | +1,173 bytes |
| resident `.data` | 7,120 bytes | 7,120 bytes | 0 |
| resident BSS | 30,928 bytes | 30,928 bytes | 0 |

The test-only `BOOT-M9.SYS` is 227,580 bytes and is neither a release artifact
nor a tracked binary.  M10 may now add the FAT16 writer and stdio/File layer;
raw Block NAPI remains deferred to M11's separate safety review.

## Files changed

- `boot98-abi.h`, `boot98-stage1.S`: service 11 and the symmetric BIOS bounce
  path.
- `boot98-stage2.c`: normal volume write callback and test-only round trip.
- `boot98-fs.[ch]`, `boot98-fat.[ch]`, `boot98-fat16.c`: stable results from
  probe through FAT-chain reads, plus the generic write-facing driver contract
  and Boolean compatibility wrappers.
- `tests/boot98-fat-host-test.c`: result, read-only, failure, bounds, and
  round-trip coverage.
- `.gitignore`, `Makefile`, `scripts/test-boot98-bios-write.sh`, and
  `scripts/install-boot98-image.sh`: isolated repeatable verification.
