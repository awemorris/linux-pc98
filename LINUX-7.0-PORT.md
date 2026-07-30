# Linux 7.0 PC-98 Port Notes

## Baseline

`linux-7.0/` is based on the official Linux v7.0 source release:

- Source: <https://www.kernel.org/pub/linux/kernel/v7.x/linux-7.0.tar.xz>
- SHA-256: `bb7f6d80b387c757b7d14bb93028fcb90f793c5c0d367736ee815a100b3891f0`

The PC-98 change set was derived by comparing this repository's
`linux-6.12/` tree with the official Linux v6.12 release, then forward-porting
that change set to v7.0. This keeps unrelated upstream changes out of the
PC-98 delta.

## Porting decisions

The v6.12 PC-98 patch produced five merge conflicts against v7.0:

- `arch/x86/Kconfig`: kept the v7.0 platform descriptions and added
  `X86_PC9800` in the extended-platform list.
- `arch/x86/boot/compressed/Makefile`: kept the v7.0 implementation because
  upstream now includes the required compiler options.
- `arch/x86/mm/init.c`: kept the v7.0 implementation because its null-address
  handling covers the older generic PC-98 fix.
- `block/partitions/Makefile`: retained both the new upstream OF parser and
  the NEC PC-98 parser.
- `block/partitions/check.h`: retained both the new upstream OF declaration
  and the NEC PC-98 declaration.

Linux 7 removed `del_timer_sync()`, so the PC-98 8251 serial driver now uses
`timer_delete_sync()`.

The resulting v7.0 PC-98 delta contains 32 files, with 2,704 insertions and
4 deletions relative to official v7.0.

## Validation

The following checks have passed:

- `CONFIG_M686=y` PC-98 `bzImage` build
- `CONFIG_M486=y` PC-98 `bzImage` build
- Full Debian-oriented build and installation of 3,867 Linux 7.0 modules
- Compilation of the PC-98 platform, NEC partition parser, PATA, keyboard,
  8251 serial, GDC text console, Cirrus fbdev, and Trident fbdev objects
- `git diff --check`
- Linux `checkpatch.pl --strict` on every newly added PC-98 source file:
  zero errors; only legacy style warnings/checks remain
- qemu-pc98 TCG boot through the native PC-98 IPL and FAT16 loader
- Mounting the Debian 13 ext4 root filesystem and reaching the login prompt
- `uname -a` reporting Linux 7.0.0 on the guest
- Release raw-image creation, xz integrity testing, and SHA-256 verification

Runtime testing of the Cirrus and Trident fbdev modules and testing on
physical PC-98 hardware remain future work.

## Build commands

Linux 7.0 i686:

```sh
KERNEL_VERSION=7.0 ./build-kernel.sh
```

Linux 7.0 i486 validation without installing modules:

```sh
KERNEL_VERSION=7.0 CPU_FAMILY=486 INSTALL_MODULES=0 ./build-kernel.sh
```

Complete Linux 7.0 Debian image:

```sh
KERNEL_VERSION=7.0 ./build-debian.sh
```

Release archive and SHA-256 file:

```sh
KERNEL_VERSION=7.0 make dist
```
