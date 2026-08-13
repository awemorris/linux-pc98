# bootsimple

bootsimple is the assembly-only PC-98 loader used by linux-pc98 BusyBox
images. The fixed-disk IPL enters LBA 2, the LBA 2 selector enters the
partition PBR, the PBR loads `IO.SYS`, and `IO.SYS` streams an uncompressed
ELF32/i386 `VMLINUX` into memory and starts Linux.

Debian images do not use this directory. They continue to use the zedBSD
boot environment in `external/zedBSD` plus the product overlay in
`bootloader/`.

The initial IPL/PBR sources were derived from `/home/awe/zedBSD` commit
`35e4718` (working tree inspected 2026-08-13):

- `bootsectors/pc98/disk-ipl.S`
  (`44e4e90881818efef328cc567f22a0f1e1e45bd6aa9bcfe44ec784fb88f80eaf`)
- `bootsectors/pc98/lba2.S`
  (`1c3e1ed556c370bdc16cc77e7e963b7b050b193d238cc1f325618667646270b6`)
- `bootsectors/pc98/partition-pbr.S`
  (`00d304bfb8d32530d93216687e1a9aa54ef35a5fb15a7c9b3956c705d8a00841`)
- `platform/pc98/fat-loader.S`
  (`c1798648555ae15fd37d8dbae0c1bc942f7f9336b71d1ca65140eafc4eeb9fdf`)

The current zedBSD bootstrap handoff is a versioned 24-byte structure.
The historical `fat-loader.S` used an older layout; `pc98/io-sys.S` must not
restore those old offsets.

Build a profile with:

```sh
./bootsimple/build.sh --profile busybox-i386-ide \
  --cmdline 'console=tty0 root=PARTLABEL=LINUXROOT rootfstype=ext4 rw'
```

Generated files are written below `build/bootsimple/` and are not tracked.

## Distribution archive

From the linux-pc98 repository, build the standalone distribution with:

```sh
./build.sh bootsimple
```

This creates `build/releases/bootsimple.zip`. The archive contains the complete
loader source, build/install/verification scripts, tests, and prebuilt loader
sets for the three public BusyBox profiles. Each profile directory includes
the exact release command line as `CMDLINE.txt`.

To replace the BOOT partition of an existing PC-98 image, extract the archive
and run, for example:

```sh
cmdline="$(cat profiles/busybox-i386-ide/CMDLINE.txt)"
./bootsimple/install-image.sh \
  --profile busybox-i386-ide --heads 8 --sectors 17 \
  --cmdline "$cmdline" disk.img vmlinux-i386
```

The selected BOOT partition is reformatted. Linux root and swap partitions are
not modified. `vmlinux-i386` is published separately with each linux-pc98
release.
