# PC-98 Bootstrap Environment: M0 Baseline

Status: **M0 COMPLETE — READY FOR REVIEW**

Recorded on 2026-08-07 before any Noct integration source change.  This is
the comparison point required by M0 of
`PC98BE-NOCT-IMPLEMENTATION-PLAN.md`.

## 1. Revisions and build host

| Item | Recorded value |
|---|---|
| `linux-pc98` | `6dbd7d6f199f44fe5ed47c3f7e3ec11104c14dd4` (`release: add ifconfig to v0.8.0 images`) |
| NoctLang | `40670781403c760a0793cfb324b2b53a4c2f0228` (`Add api-thread.c (wip)`) |
| Host | Debian 13, `debian13` |
| GCC | Debian GCC 14.2.0-19 |
| binutils | GNU binutils 2.44 |
| CMake | 3.31.6 |
| Ninja | 1.12.1 |

The initial `linux-pc98` worktree contained only the approved plan and its
README link as uncommitted changes.  Submodule revisions were recorded by
`git submodule status`; M0 did not alter them.

## 2. Existing BOOT98 checks

Command:

```sh
make -C bootloader all check
```

Result: **PASS**.  The FAT16 host test printed
`BOOT98 FAT16 host tests: OK`.

The generated `BOOT.SYS` and linked ELF were hashed before and after the
test.  Both were unchanged:

| File | SHA-256 |
|---|---|
| `bootloader/BOOT.SYS` | `b4ea7d4e43b1442eb48cbb423454f25606ff275d49b68aadc0dabc4caa2dd81b` |
| `bootloader/boot98-stage2.elf` | `9dd278a48c12b9b288b4d6c58a3bf6a598213121d4f39672b6ac771cddc1ab3a` |

`make check` creates the untracked host executable
`bootloader/tests/boot98-fat-host-test`; it was removed after the test.

## 3. Headless BusyBox regression

Canonical command:

```sh
./build.sh test busybox-i386 \
  --image build/tests/busybox-i386/test.raw \
  --timeout 180 \
  --log /tmp/pc98be-m0-qemu-serial-180.log
```

Result: **PASS (with an automation caveat)**.  The first run had to populate the normal
`build/i386-buildroot` and `build/tests/busybox-i386/test.raw` caches.  A
70-second run produced no serial text, so M0 reran the same fixed image for
180 seconds rather than treating an empty timeout as success.  The second
run also produced a zero-byte serial log.

The generated FAT16 `BOOT.CFG` was inspected directly.  Its loader command
line was:

```text
arg root=PARTLABEL=LINUXROOT rootfstype=ext4 rw
```

It omits explicit console arguments, but the dual-console kernel's built-in
command line correctly retains `console=ttyPC0 console=tty0`; the kernel log
confirmed this.  Adding the same arguments in a diagnostic image merely
duplicated them and was not required.

The actual observation problem was the detached GNU Screen path around
`-serial stdio` and `tee`: it left a zero-byte log even while QEMU ran.  M0
therefore ran the original, unmodified fixed image with the same machine,
CPU, RAM, firmware, storage, TCG, and no-network settings, changing only the
QEMU chardev to `-serial file:/tmp/pc98be-m0-original-serial.log`.

That run reached all required checkpoints in about 25 seconds:

```text
Linux version 7.1.0
pc9800-8251: ttyPC0 at I/O 0x30
pc98ide: hda: 884952 sectors (432 MiB), LBA28 polling PIO
EXT4-fs (hda2): mounted filesystem ... r/w
VFS: Mounted root (ext4 filesystem) on device 254:2.
Run /sbin/init as init process
Adding 32772k swap on /dev/hda3.
BusyBox v1.38.0 ... built-in shell (ash)
#
```

No panic appeared.  Product regression is therefore **PASS**.  A future
test-harness cleanup should offer a file-backed serial capture mode so
detached automation does not depend on a pseudo-terminal, but it is not a
BOOT98/Noct integration blocker.

## 4. Noct upstream host baseline

Noct was configured as a release static build with the CLI, JIT, built-in
i18n, standard API, optional backends, and REPL enabled.  The build completed
successfully.

Command:

```sh
cd /tmp/noctlang-plan/tests
./run-syntax.sh
```

Result: **PASS**.  All 40 syntax cases passed in both interpreter mode
(`--disable-jit`) and JIT mode (`--force-jit`).

## 5. Current BOOT.SYS memory map

The current linker image starts at `0x00020000`.  Values below come from
`size`, `readelf`, `nm`, and the final binary rather than estimates.

| Measurement | Value |
|---|---:|
| `BOOT.SYS` file/load bytes | 14,028 |
| `.text` | 12,998 bytes |
| `.rodata` | 992 bytes |
| `.data` | 12 bytes |
| `.bss` section | 10,560 bytes |
| ELF load segment `MemSiz` | 24,608 bytes |
| `__image_end` | `0x000236cc` |
| `__bss_end` | `0x00026020` |
| Remaining load-image space before `0x00060000` | 248,116 bytes |
| Remaining low-memory space before `boot_params` at `0x00070000` | 303,072 bytes |

The final linked ELF has no undefined symbols.  These measurements confirm
the plan's approximate 14 KiB baseline and make clear that the nominal
256 KiB load window has 248,116 bytes left, not another full 256 KiB.

## 6. Selected Noct source baseline

The initial product subset is the generated lexer/parser plus `ast.c`,
`hir.c`, `lir.c`, `noct.c`, `runtime.c`, `interpreter.c`, `jit.c` (which
selects `jit-x86.c` for this target), `execution.c`, `gc.c`, `intrinsics.c`,
and `objectmodel-st.c`.  CLI, REPL, translation, optional compiler backends,
and the multithreaded object model are excluded.  `api-file.c` is deferred
until BE stdio exists.

For dependency measurement, that core-only source set was built on the host
with JIT enabled and all optional components disabled, then combined with
`ld -r --whole-archive`.  The host x86-64 object is not a product size
estimate, but supplies a reproducible unresolved-symbol inventory.

### 6.1 Core external dependencies

```text
abort atof atoi atoll calloc clearerr cosf __errno_location exit ferror
fread free fwrite getc _GLOBAL_OFFSET_TABLE_ __isoc99_sscanf malloc memmove
mmap mprotect munmap printf putchar puts rand sinf snprintf sqrtf strcmp
strcpy strdup strlen strncmp strncpy tanf vsnprintf
```

The selected host object measured 149,937 bytes of text, 12,744 bytes of
data, and 28,000 bytes of BSS.  This is a host-ABI ceiling/inventory aid only;
the M1/M2 i386 freestanding object build is the first valid BOOT.SYS size
measurement.

### 6.2 Header surface

The selected core currently reaches the C interfaces in `assert.h`,
`errno.h`, `inttypes.h`, `limits.h`, `math.h`, `stdarg.h`, `stdio.h`,
`stdlib.h`, `string.h`, `time.h`, and `unistd.h`.  The generic host JIT path
also reaches `sys/mman.h`; M1 must replace that path through the approved
PC98BE JIT allocation/protection hooks.

### 6.3 Deferred standard File API

`src/api/api-file.c` additionally requires the following host-facing calls:

```text
access fclose fgets fopen fprintf fread free fseek ftell fwrite malloc
strcmp strlen
```

It also depends on public Noct registration, value, packed-buffer, callback,
and dictionary APIs.  This confirms that implementing an honest BE `FILE`
surface is sufficient; a second language-level File API is not required.

## 7. M0 conclusion

The existing BOOT98 build/host test, original i386 BusyBox QEMU image, and
upstream Noct interpreter/JIT syntax baseline all pass.  No product source
behavior has been changed.  M1 may start after this M0 document and its
known detached-stdio test-harness caveat are reviewed.
