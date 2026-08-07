# PC98BE M10: FAT16 Writer and Noct File API

M10 makes ordinary files on the selected BOOT FAT16 filesystem writable from
Noct.  It deliberately does not expose raw sectors, remove, rename, or a
general block-device API.  Those operations remain outside this milestone's
safety boundary.

## Layering

The write path is kept in four independently testable layers:

```text
Noct File / FileUtil API
        |
BE stdio adapter (FILE, fopen/fread/fwrite/fseek/...)
        |
generic boot98 filesystem interface
        |
FAT16 driver -> 512-byte physical-sector cache -> BIOS volume callback
```

`api-file.c` remains target-independent Noct code.  PC98BE-specific filesystem
selection and stream lifetime are implemented below stdio.  The upstream
Noct worktree and `third_party/noct` carry byte-identical `api-file.c` changes;
after the upstream change is committed, the subtree snapshot metadata must be
refreshed to that commit before importing a later upstream snapshot.

## FAT16 write rules

The writer preserves the existing model of 512-byte physical I/O even when a
BPB declares 1024-byte logical sectors.  A result-aware cache owns one physical
sector and has explicit dirty, flush, and invalidate states.  BPB storage is
separate from this cache, so mounting a 1024-byte-logical-sector volume cannot
overwrite cache state.

Supported operations are root-directory 8.3 create/open/stat, truncate,
offset write, chain extension, and flush.  Both FAT copies are updated.  Free
cluster search is bounded and wraps once from the remembered allocation hint.
New clusters are zero-filled before their links and final directory size make
the data visible.  A flush writes pending data/FAT sectors before the root
directory metadata that publishes first-cluster and file-size changes.

The driver rejects a read-only volume, unsupported BPB, full root directory,
full data area, arithmetic overflow, invalid cluster, and cyclic chain with a
stable `boot98_fs_result`.  Long names, subdirectory creation, remove, rename,
and crash-atomic transactions are not implemented.

## BE stdio contract

The freestanding adapter implements:

- `fopen` with `r`, `rb`, `w`, and `wb`;
- `fclose`, `fflush`, `fread`, `fwrite`, and `getc`;
- absolute `fseek`, `ftell`, `fgets`, `fprintf`, and `access`;
- console forwarding for `stdout` and `stderr`;
- an intrusive list of open streams and `boot98_stdio_close_all()`.

Opening with `w` creates or truncates the file.  Seek is bounded by the FAT16
driver's 32-bit file-size representation.  Every filesystem result is mapped
to the small BE `errno` set, including `ENOSPC`, `EROFS`, and `EOVERFLOW`.
`FILE` is private to this libc and does not alias the generic
`struct boot98_file` tag.

## Noct ownership and cleanup

The standard Noct `File` and `FileUtil` dictionaries are registered after the
PC98BE native APIs.  `File.open` installs a finalizer only after `fopen`
succeeds.  Explicit close clears the native pointer before `fclose`; failures
after ownership transfer either clear ownership and close once, or leave the
stream to the finalizer.  Closed handles are rejected.

All pinned locals, temporary buffers, and streams are released on every error
path.  After VM destruction, PC98BE closes any remaining streams before
resetting the arena and clearing the active filesystem.  This also handles a
script that intentionally leaks a `File` handle.  Noct's packed value currently
cannot represent an empty byte array, so `File.read(0)` is rejected explicitly.

## Verification evidence

The following completed successfully on 2026-08-08:

| Check | Result |
| --- | --- |
| FAT16 create/overwrite/gap/extend/shrink/zero truncate | PASS |
| 512- and 1024-byte BPB logical-sector images | PASS |
| Both FAT copies and allocated zero-length file | PASS |
| Full root, full disk, cyclic chain, injected write failure | PASS |
| BE stdio modes, seek/tell, text I/O, errors, close-all | PASS |
| Noct File/FileUtil and leaked-handle cleanup host tests | PASS |
| i386 opcode audit for libc, soft-float, Noct, and final image | PASS |
| Historical M1-M9 host and IDE/SCSI QEMU regressions | PASS |
| QEMU script writes `M10.TXT`; independent host `mtools` read | PASS |
| Upstream Noct interpreter and JIT syntax suites | 40/40 each PASS |

Reproduce the integrated tests with:

```sh
make -C bootloader noct-m10-verify -j32
```

The independent upstream Noct check used:

```sh
cd ~/NoctLang
cmake --build --preset static -j32
cd tests && ./run-syntax.sh
```

## Size impact

The committed M9 normal build is the baseline documented in
`PC98BE-NOCT-M9-WRITES.md`.

| Region | M9 | M10 | Delta |
| --- | ---: | ---: | ---: |
| `BOOT.SYS` load image | 226,832 | 238,340 | +11,508 bytes |
| resident `.text` | 219,691 | 229,425 | +9,734 bytes |
| resident `.data` | 7,120 | 8,900 | +1,780 bytes |
| resident BSS | 30,928 | 31,504 | +576 bytes |

The reviewed M10 `BOOT.SYS` SHA-256 is
`34bac5b30d47a55082df028c6a10f867b89fdfd866ea8d11ea00aaf7fea41135`.

## Files changed

- `boot98-fat.[ch]`, `boot98-fat16.c`: physical-sector cache and FAT16 writer.
- `tests/boot98-fat-{host,write-host}-test.c`: read regressions and destructive
  in-memory writer coverage.
- `libc/boot98-stdio-fs.[ch]`, `libc/boot98-stdio.c`, libc headers and make
  fragments: freestanding filesystem-backed stdio.
- upstream and vendored `src/api/api-file.c`: target-independent File API
  correctness and lifetime fixes.
- `boot98-noct.[ch]`, `boot98-noct-platform.c`, `noct.mk`, and Noct host tests:
  registration, active-filesystem injection, cleanup, and integration.
- `scripts/test-boot98-noct-file.sh` and `Makefile`: repeatable QEMU marker-file
  test and aggregate M10 verification.
