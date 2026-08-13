Linux/pc98 Workspace
====================

This repository contains:

- Linux: 7.1, i386SX/DX and i486SX/DX port, with NEC PC-9800 machine support
- Debian: 13 "trixie", i486DX port, with NEC PC-9800 utils
- glibc: 2.41, i486DX port
- BusyBox: i386SX port
- qemu: 11.0, NEC PC-9800 support with compatible BIOS
- bootsimple: assembly-only FAT16 VMLINUX loader for the BusyBox images
- zedBSD: boot OS used by the Debian images (external/zedBSD submodule)

Each component is reusable for other retro PCs.

## How to start

Clone it with its maintained toolchain and emulator sources:

```sh
git clone --recurse-submodules https://github.com/awemorris/linux-pc98.git
```

For an existing checkout, initialize the same sources with:

```sh
git submodule update --init --recursive
```

## Supported PC-98 CPU generations

32-bit PC-98 systems were sold with i386SX, i386DX, i486SX, i486DX, Pentium,
Pentium MMX, Pentium II, and Pentium-II-class Celeron processors. Linux/PC-98
has been ported to the i386SX, i386DX, i486SX, i486DX, and i686 execution
classes needed to cover these machines.

| Physical CPU | Linux build | Userland | Packages | Minimum RAM |
| --- | --- | --- | --- | ---: |
| i386SX or i386DX | PC-98 i386 | i386SX-compatible static musl/BusyBox | included in the CF image | 5 MiB |
| i486SX | PC-98 i486 with software floating point | static musl/BusyBox | included in the CF image | 5 MiB |
| i486DX, Pentium, or Pentium MMX | PC-98 i486 | Debian 13/i486DX with the maintained glibc i486 port | project-built repository; publication pending | 64 MiB |
| Pentium II or Pentium-II-class Celeron | PC-98 i686 | Debian 13/i686 | official Debian packages | 64 MiB |

The i386 release image uses the i386SX baseline and therefore also runs on
i386DX systems. The custom Debian port deliberately uses i486DX, including
its hardware floating-point unit, as its minimum ABI. It is also the Debian
choice for Pentium and Pentium MMX systems, which do not satisfy the i686
baseline used by current official Debian packages. Pentium II systems can
run the official Debian userland unchanged with the PC-98 i686 kernel.

The custom i486 package repository is not public yet. Publishing its packages
is the next distribution milestone. Testing has established that both Debian
variants require at least 64 MiB of physical RAM for reliable operation; the
smaller BusyBox systems are the supported choice below that threshold. The
published i486 image bypasses `/bin/login` and PAM and starts a root `/bin/sh`
directly, avoiding login timeouts and memory exhaustion on slower machines.

## Repository layout

| Path | Contents |
| --- | --- |
| `external/kernel/linux-2.6.7-pc98-original/` | Immutable last-complete upstream PC-9800 source snapshot from immediately before its 2004 removal |
| `external/kernel/linux-7.1/` | Linux 7.1 source tree with the clean PC-98 port integrated |
| `external/qemu-pc98/` | qemu-pc98 submodule used for i386 and PC-98 validation |
| `external/gcc`, `external/musl`, `external/glibc` | Toolchain submodules; versioned patch inventory in `external/patchsets/` |
| `external/debian-i486/` | Framework and patch database for the Debian 13/i486DX package port |
| `configs/` | Debian-derived i686 base and versioned PC-98 configurations |
| `bootsimple/` | Assembly PC-98 IPL, FAT16 PBR, and direct VMLINUX loader for BusyBox images |
| `external/zedBSD/` | zedBSD boot OS submodule used as the Debian image bootloader and menu |
| `external/noct/` | NoctLang submodule used to precompile Remacs for product images |
| `bootloader/` | Linux product overlay; `fs/` mirrors its BOOT FAT placement |
| `scripts/` | Internal build, image, test, publication scripts and disk-image tools |
| `build/` | Generated kernel, rootfs, logs, and disk images; ignored by Git |
| `build.sh` | Single supported entry point for all project builds |

Nix is not required. The build uses standard packages available on Debian 13.
See `external/TOOLCHAIN.md` for the exact source baselines, local-source
integration, patch regeneration, update procedure, and automated patch
replay check.

The historical Linux 2.6.7 tree is retained as immutable source and
provenance material; it is not used by the build. The direct clean port to
the current Linux 7.1 tree is documented in `external/kernel/audit/`.

## glibc 2.41 ports

The supported Debian target is the glibc 2.41 i486DX port maintained in the
`external/glibc` submodule. It uses the i486 native atomic instructions and
serves i486DX, Pentium, and Pentium MMX machines. An exact-i386 glibc research
target also passes its dedicated validation suite, but it is not a Debian
distribution target. It depends on a versioned kernel atomic syscall because
the original 80386 lacks `CMPXCHG` and `XADD`.

After the static-musl i386 Buildroot toolchain has been built, the complete
validation workflow is:

```sh
./build.sh glibc i486
./build.sh glibc-tests i486
./scripts/build-glibc-busybox.sh i486
```

For exact-i386 research, replace `i486` with `i386` and additionally run
`scripts/check-glibc-i386-opcodes.sh`.

See `external/patchsets/glibc/` for the per-release patch inventory, security
constraints and validation notes covering the exact-i386 research work. Debian
i486 packaging is tracked in `external/debian-i486/`.

## Host requirements

On Debian 13, install the build, image-generation, and headless-test
dependencies with:

```sh
./build.sh setup
```

The package inventory is kept in `scripts/setup-packages.txt` and can still be
passed to `apt` manually. Use `./build.sh setup --help` for unattended and
`apt update` controls.

Root privileges are used when creating the Debian staging tree, installing
kernel modules into it, and generating the ext4 filesystem image.

The Debian images retain the prebuilt
`external/zedBSD/platform/pc98/dos/linux98.exe`, stored as `LINUX98.EXE` in
their FAT16 boot partitions. BusyBox images do not contain this DOS loader:
their assembly-only `IO.SYS` loads VMLINUX directly. Normal builds therefore
do not require OpenWatcom. Maintainers rebuilding `LINUX98.EXE` must install
OpenWatcom 1.9 and run `./build.sh dos-loader`; set `WATCOM` if it is not
installed at `/home/awe/openwatcom-1.9`.

### Docker build environment

The development image installs the same Debian 13 dependencies but keeps the
source tree outside the container. Build it from the repository root and mount
the checkout at `/work/linux-pc98`:

```sh
docker build -t linux-pc98-build .
docker run --rm -it \
  -v "$PWD:/work/linux-pc98" \
  linux-pc98-build ./build.sh --help
```

Pass `--build-arg HOST_UID=$(id -u) --build-arg HOST_GID=$(id -g)` when the
host user is not UID/GID 1000. Add `--device /dev/kvm` only when a test
explicitly uses KVM; the normal PC-98 smoke tests use TCG. OpenWatcom is not
redistributed in the image and must be mounted separately when building
`LINUX98.EXE`; ordinary image builds use its checked-in binary.

## Headless serial-console tests

Public images continue to use the PC-98 VRAM console. Reproducible private
test images with a PC-98 serial console can be prepared and run with:

```sh
./build.sh test busybox-i386
./build.sh test debian13-i486
```

The guest serial port is attached to the current terminal. Use
`--prepare-only` to build without starting QEMU, or run a bounded smoke test:

```sh
./build.sh test debian13-i486 --timeout 120 \
  --log build/tests/debian13-i486/serial.log
```

Run `./build.sh test --help` for QEMU, BIOS, memory, and image overrides.

## Complete Debian image build

Linux 7.1 is the only maintained kernel tree and the default build target:

```sh
./build.sh debian
```

On the first run, this script creates a rootfs from Debian trixie's official
`i386` archive in `build/debian-i386-root`. Despite the Debian architecture
name, these official binaries use an i686-class baseline and are intended for
Pentium II or newer systems. The script then builds Linux, installs the
modules, builds the PC-98 loaders, and creates:

```text
build/qemu-pc98-linux-7.1.raw
```

The kernel is built out of tree in `build/kernel-7.1`; `external/kernel/linux-7.1/` remains
a source-only directory. `KERNEL_VERSION=7.1` may still be specified
explicitly for automated builds.

The individual build stages can also be run separately:

```sh
./build.sh rootfs debian13-i486
./build.sh kernel --cpu 486
./build.sh image debian13-i486-ide
```

To build the complete public artifact set with canonical filenames:

```sh
./build.sh release --version v0.6.0
```

This is the only supported command for a public Release. It builds all six
disk images, standalone kernels and loaders, and `qemu-pc98-win64.zip` under
`build/releases/`. `XZ_LEVEL` and `XZ_THREADS` can override compression.
The fixed Release text is read from `releases/v0.6.0.md`. Maintainers may also
pass `--publish-rootfs-cache` to refresh the reusable rootfs archives on the
package server.

### Named image profiles and reusable bases

Use `./build.sh image list` to list the supported variants.  Every image is
created through a named profile, for example:

```sh
./build.sh image busybox-i386-ide
./build.sh image busybox-i386-scsi55
./build.sh image busybox-i386-scsi92
./build.sh image debian13-i486-ide
./build.sh image debian13-i486-scsi92
./build.sh image debian13-i486-scsi55
```

The commands above are intended for development and may use `--output` for
temporary experiments.  Images handed to testers or uploaded as release
artifacts must instead be generated with the canonical command:

```sh
./build.sh release-image busybox-i386-ide
./build.sh release-image busybox-i386-scsi55
./build.sh release-image busybox-i386-scsi92
./build.sh release-image debian13-i486-ide
./build.sh release-image debian13-i486-scsi55
./build.sh release-image debian13-i486-scsi92
```

Each command writes only its fixed filename under `build/releases/`.  It
builds a temporary image in the same directory and atomically replaces the
canonical image after successful completion, together with its SHA-256 file.
`./build.sh release-image all` rebuilds all six variants. Diagnostic suffixes
must not be added to files in `build/releases/`. Use `./build.sh release` for
the complete compressed public artifact set.

The BusyBox i386 IDE image is below 160 MiB and uses H=8/S=17, which matches
most PC-98 IDE BIOS implementations. Only IDE images of 20 MiB or less use the
legacy small-disk geometry H=4/S=17. Other IDE profiles use H=8/S=17. PC-9801-92 SCSI
profiles use H=8/S=32 and therefore require a separate disk image even when
the files stored in the partitions are identical. PC-9801-55-compatible
profiles, including WINnote98, use H=8/S=17. Every release image has a
128 MiB FAT16 BOOT partition. Debian images put zedBSD's 64 MiB `/swapfile`
there; BusyBox images reserve the same partition size but contain only the
direct-loader files and VMLINUX. This swapfile is separate from the Linux
swap partition. The old profile names ending in plain `-scsi` remain aliases
for `-scsi92`. Partition sizes are defined in
bytes and rounded up to whole cylinders, so changing geometry does not nearly
double the SCSI image capacity. The i386 IDE image uses the small `pc98_ide`
driver (`/dev/hda`, with a second disk at `/dev/hdb`); i386 SCSI and both
i486/libata images use the standard
SCSI disk namespace (`/dev/sda`).

Both bootsimple and zedBSD/`LINUX98.EXE` pass the BIOS SENSE drive number and
logical H/S to Linux in `SETUP_PC98_DISK` setup-data records. bootsimple also
enumerates additional BIOS IDE drives. The PC-9801-55/92 driver uses the
record when decoding the boot disk's NEC98 partition table. For an older
loader which does not provide it, `pc9801_scsi=55` selects the 8/17 fallback;
the default `pc9801_scsi=92` fallback is 8/32.

The same argument can override the Linux IRQ and PC-98 DMA channel for a
fixed-configuration or compatible board:

```text
pc9801_scsi=55,irq=5,dma=0,clock=12
pc9801_scsi=92,irq=5,dma=0,clock=8
```

Valid Linux IRQ values are 3, 5, 6, 9, 12, and 13; valid DMA channels are
0, 2, and 3.  A plain `pc9801_scsi=55` selects the fixed WINnote98/
PC-9801-55 defaults IRQ 5 (board INT1), DMA 0, and the WD33C93 12--15 MHz
clock range used by the historical Linux/98 driver. `clock=` accepts 8, 12,
or 16 for the controller's 8--10, 12--15, or 16--20 MHz range. With the 92
profile, omitted IRQ and DMA values continue to be read from the board
configuration registers and the clock defaults to the 8--10 MHz range.

`mode=dma` and `mode=async-pio` select the data-transfer path.  The 92
profile defaults to the historical DMA path.  The 55 release images pass
`mode=async-pio`: on the physically tested WINnote98 the board's DMA
gate never delivers a request to the PC-98 DMA controller (its count
register stays at the programmed value), so the DMA path cannot work
there, while asynchronous PIO boots reliably.  Boards with a working
DMA path can still request `mode=dma` explicitly.

A BlueSCSI target on PC-98 requires a `bluescsi.ini` on its SD card;
without it the firmware's Apple-oriented default quirks stall the
PC-9801-55/92 driver at the phase level:

```ini
[SCSI]
Quirks = 0
EnableSCSI2 = 0
MaxSyncSpeed = 5
Debug = 0
```

BusyBox images use bootsimple. Its LBA0 enters LBA2, the selector loads the
PBR of the FAT16 partition named BOOT, and the PBR loads contiguous `IO.SYS`
as an ordinary FAT file. `IO.SYS` is assembly code which SENSEs the disk,
loads the root-directory file `VMLINUX` directly as ELF32, constructs Linux
boot parameters and PC-98 disk setup-data, then enters Linux. Each phase is
shown on the PC-98 text screen so a real-machine stop can be localized.
`./build.sh bootsimple-test` verifies the loader and generated-image layout.

Debian images retain zedBSD as the graphical boot OS. zedBSD starts `/bin/sh`;
the Linux product overlay supplies `/etc/zinit.rc`, the Noct menu, Emacs and
Holoris, and starts Linux through `/bin/linux`. A bare zedBSD image has no
startup script and enters the shell directly.

`INST.EXE` performs the three raw installation operations needed after a DOS
partitioning/formatting tool has prepared the target volume.  Keep
`IPL-LBA0.IMG`, `IPL-LBA2.IMG`, `IPL-PART.IMG`, and `IO.SYS` beside
`INST.EXE`. Select the
target partition as the current DOS drive before installing the disk IPL:

```dos
C:
A:\INST /LBA0
A:\INST /LBA2
A:\INST /PART C:
```

The first two operations map the current DOS drive back to its physical IDE
disk. `/PART` copies `IO.SYS` as a hidden/system/read-only FAT16 file,
verifies its traditional contiguous system-file layout, and installs the
1024-byte PBR while preserving the BPB. `BOOT.SYS`, `VMLINUX`, and
`BOOT.CFG` remain ordinary DOS file copies.

Every generated BOOT partition also contains `INST.EXE`, `IPL-LBA0.IMG`,
`IPL-LBA2.IMG`, and `IPL-PART.IMG`, so the installed image itself can be used
as the source for installing BOOT98 onto another DOS-visible disk.

A test kernel can be installed while creating an image:

```sh
./build.sh image debian13-i486-ide \
  --kernel build/test-kernel/vmlinux.boot \
  --output build/images/test-kernel.raw
```

To derive a test image without rebuilding its root filesystem, pass either a
local raw/raw.xz file or a package-server base name:

```sh
./build.sh image debian13-i486-ide \
  --base-image debian13-i486-boot98-base \
  --kernel build/test-kernel/vmlinux.boot \
  --output build/images/test-from-base.raw
```

Fetched archives are verified and retained in
`${XDG_CACHE_HOME:-~/.cache}/linux-pc98/images`.  Maintainers can publish an
exact base without exploring the rental server:

```sh
./build.sh cache publish debian13-i486-boot98-base \
  build/images/debian13-i486-ide.raw
```

This updates only the documented
`www/noctvm.io/debian-i486/images/pc98/bases/` directory and its matching
SHA-256 file.

Root filesystem trees are cached independently from complete disk images:

```sh
./build.sh rootfs-cache fetch busybox-i386-video-buildroot-2026.05
./build.sh rootfs-cache materialize debian13-i486-trixie-v1 build/rootfs-copy
./build.sh rootfs-cache store NAME ROOTFS-DIR
./build.sh rootfs-cache publish NAME ROOTFS-DIR
```

Local archives live under `${XDG_CACHE_HOME:-~/.cache}/linux-pc98/rootfs`.
Fetches use HTTPS and verify both SHA-256 and xz integrity. Publication writes
only to `www/noctvm.io/debian-i486/images/pc98/rootfs/`.

The v0.6.0 Release set consists of the following persistent CF/HDD images.
The Debian images use the full PC-98 kernel configuration and require 64 MiB.

| Image | Geometry and purpose | Userland | Minimum RAM | Raw size limit |
| --- | --- | --- | ---: | ---: |
| `linux-pc98-i386sx-busybox-ide.img.xz` | IDE H=8/S=17 on i386SX/DX machines | static musl/BusyBox | 5 MiB | below 160 MiB |
| `linux-pc98-i386sx-busybox-scsi55.img.xz` | PC-9801-55-compatible SCSI H=8/S=17, including WINnote98 | static musl/BusyBox | 5 MiB | below 160 MiB |
| `linux-pc98-i386sx-busybox-scsi92.img.xz` | PC-9801-92 SCSI H=8/S=32 | static musl/BusyBox | 5 MiB | below 160 MiB |
| `linux-pc98-i486dx-debian13-ide.img.xz` | IDE H=8/S=17 on i486DX or newer | Debian 13/i486DX | 64 MiB | below 2 GB |
| `linux-pc98-i486dx-debian13-scsi55.img.xz` | PC-9801-55-compatible SCSI H=8/S=17 | Debian 13/i486DX | 64 MiB | below 2 GB |
| `linux-pc98-i486dx-debian13-scsi92.img.xz` | PC-9801-92 SCSI H=8/S=32 | Debian 13/i486DX | 64 MiB | below 2 GB |

Public images use the GDC screen and PC-98 keyboard as the console. The same
release also contains `vmlinux-i386`, `vmlinux-i486-debian`, `linux98.exe`,
`inst.exe`, `ipl-lba0.bin`, `ipl-lba2.bin`, their DOS `.img` copies,
`ipl-part.img`, `IO.SYS`, `BOOT.SYS`,
`boot.cfg`, and
`qemu-pc98-win64.zip`. The Windows ZIP contains both i386 and x86_64 QEMU,
`virtpc98.exe`, required DLLs, and the free PC-98 BIOS/SCSI BIOS files.

The i386 BusyBox kernel includes the PC-9801-55/92-compatible host adapter,
SCSI core, and SCSI disk support as built-ins (`=y`), so a SCSI root or
utility disk does not depend on modules from the low-memory root filesystem.

## Disk layout

`build/qemu-pc98-linux.raw` is a single raw IDE disk with two native PC-98
partitions.

| Region | Contents |
| --- | --- |
| LBA 0 | `ipl-lba0.bin`, with the `IPL1` marker |
| LBA 1 | PC-98 sixteen-entry partition table |
| LBA 2 through 15 | Replaceable `ipl-lba2.bin` BOOT-partition selector |
| BOOT partition reserved sector | 1024-byte FAT16 PBR/BPB (`ipl-part.img`) |
| BOOT partition data area | FAT16 containing contiguous `IO.SYS`, `BOOT.SYS`, `boot.cfg`, non-compressed `VMLINUX`, and `LINUX98.EXE` |
| Partition 2 | ext4 root filesystem, mounted as `/dev/sda2` |

Native PC-98 images carry both the `IPL1` marker at offset 4 and the `55 AA`
signature at bytes 510–511 of LBA 0.  Some later PC-9821 firmware requires the
trailing signature before it will execute a hard-disk IPL.  Linux gives a valid
NEC98 table at LBA 1 priority when `IPL1` is present, even though `55 AA` is
also present; an unmarked `55 AA` disk is left to the normal MS-DOS partition
parser.

The distributed LBA 2 selector and the NEC fixed-disk boot menu both enter the
same partition PBR. Its IPL and data-start CHS values are identical. The PBR
loads the contiguous FAT file `IO.SYS`; the reserved 1024 bytes are outside
the FAT cluster area and do not duplicate any part of that file.

`./build.sh boot-install --partition N IMAGE VMLINUX BOOT.CFG` destructively
recreates only the selected BOOT partition and preserves an NEC or third-party
disk IPL at LBA 0 and LBA 2–15. Add `--install-disk-stubs` when the distributed LBA 0
and LBA 2–15 stubs should also be installed.

The kernel no longer overrides the command line supplied by BOOT98.
`BOOT.CFG` in each image selects the matching root device. A manually
installed standalone configuration should therefore use
`root=PARTLABEL=LINUXROOT` (or `/dev/hda2` for a fixed single-disk setup) for the
i386 IDE image and `/dev/sda2` for i386 SCSI or i486/libata images.

DOS may install its own PBR and filesystem in Partition 1. Reformatting it
removes `VMLINUX`, so restore the kernel afterwards if the partition is to
remain Linux-bootable.

The Linux port includes a PC-98 partition-table parser so both partitions are
reported through the normal Linux block-device interface.

The `debian13-i486-*` profiles additionally append a 128 MiB swap
partition as `/dev/sda3`. Its initial SysV networking policy is disabled in
`/etc/default/networking`, preventing boot from waiting for DHCP on machines
without a configured NIC. Enable it after installing and configuring the
desired PC-98 network adapter.

## Updating only the kernel

To update `VMLINUX` in Partition 1 without rebuilding or modifying the
Debian userland in Partition 2, derive a new image from an existing raw
image and a stripped, non-compressed ELF vmlinux:

```sh
./build.sh image debian13-i486-ide \
  --base-image path/to/disk.raw \
  --kernel path/to/vmlinux.boot \
  --output build/images/test.raw
```

## Build configuration

The main environment-variable overrides are:

| Variable | Default or purpose |
| --- | --- |
| `KERNEL_VERSION` | `7.1` (the maintained kernel source) |
| `KERNEL_SOURCE` | `linux-$KERNEL_VERSION` |
| `JOBS` | `nproc`; parallel kernel build job count |
| `KERNEL_BUILD` | `build/kernel-$KERNEL_VERSION` |
| `CPU_FAMILY` | `686`; use `486` or `386` for the completed PC-98 low-generation ports |
| `DEVICE_PROFILE` | Linux 7.1 defaults to `pc98`; use `full` for the full Debian driver catalogue |
| `CONSOLE_MODE` | `video`; use `dual` only for a private GDC plus serial diagnostic build |
| `INSTALL_MODULES` | `1`; set to `0` to skip `modules_install` |
| `OUTPUT_IMAGE` | Version-specific raw image path |
| `ROOT_STAGE` | `build/debian-i386-root` |
| `DEBIAN_SUITE` | `trixie` |
| `DEBIAN_MIRROR` | Official Debian mirror |
| `DEBIAN_INCLUDE` | Comma-separated packages added to the rootfs |
| `ROOT_PASSWORD` | Initial local test password; default `pc98` |
| `BOOT_MB` | FAT16 boot partition size for BusyBox profiles; default 128 MiB |
| `ZEDBSD_SWAPFILE_MB` | zedBSD BOOT swapfile size for Debian images; default 64 MiB (`0`, `32`, or `64`); BusyBox bootsimple images require `0` |
| `ROOT_MB` | ext4 root partition size; default 200 MiB |
| `BOOT_LOGO` | Optional 80 by 120 packed 1bpp logo for the boot screen |
| `DIST_IMAGE_NAME` | Filename inside `dist/` before the `.xz` suffix |

The Debian rootfs builder stops if `ROOT_STAGE` already exists, preventing an
existing rootfs from being overwritten accidentally.

Use the separately maintained Debian/i486DX port rather than the official
Debian archive on i486DX, Pentium, and Pentium MMX systems.

Linux 7.1 uses the `pc98` device profile by default. It retains the PCI core
required by `pc9821`, the PC-98 IDE and framebuffer drivers, and standard
USB 1.x/2.0 UHCI/OHCI/EHCI host controllers. The framebuffer console is
built in. The Debian/i686 configuration keeps the Core-Graph Cirrus driver
as a module, while the i386 and i486 configurations disable it completely.
This prevents an untested model from replacing the working GDC console or
freezing during boot. The USB module set is limited to generic HID,
mass-storage, CDC Ethernet/NCM, ACM serial, and printer classes. The fixed
module allow-list is stored in
`configs/pc9800-modules.list`; it does not depend on the build host's loaded
modules. This reduces the configured module count from 3,644 in the full
Debian configuration to 23 modular Kconfig entries (22 built `.ko`
files).

The untrimmed Debian driver catalogue remains available for comparison:

```sh
./build.sh kernel --cpu 686 --profile full
```

## Linux 7.1 port status

The `external/kernel/linux-7.1/` tree is based on the official Linux v7.1 release. PC-98
support was cleanly and directly reconstructed from the official final
Linux/PC-98 2.6.7 sources, with current APIs and independently maintained
project code documented in `external/kernel/audit/PC98-PORTING-REPORT.md`.
Linux 7.1 changed
partition-parser logging to `struct seq_buf`; the NEC98 parser follows the
new API.

Linux 7.1 supports the PC-98 `CONFIG_M386=y`, `CONFIG_M486=y`, and
`CONFIG_M686=y` targets. The lower-generation work restores the x86 i386 and
i486 configurations removed by upstream Linux and supports i386SX, i386DX,
i486SX, and i486DX hardware. The small i386 and i486 release images use a
static musl/BusyBox userland; the i486DX release additionally provides the
custom Debian port.

The trimmed kernel and module set build successfully. The generated
two-partition image boots under qemu-pc98 with TCG, mounts the Debian 13 ext4
root filesystem, reaches the login prompt, and reports Linux 7.1.0 i686.

To build and run the small BusyBox image:

```sh
./build.sh image busybox-i386-ide

qemu-system-i386 \
  -M pc9801 \
  -cpu 386 \
  -m 8 \
  -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=build/images/busybox-i386-ide.raw
```

Use the named `build.sh image` profiles for release and routine test images.
Set `BUSYBOX_WORK` or `ROOT_STAGE` to override their default build and staging
directories when invoking the lower-level scripts directly.

The i486 image mounts its ext4 root, starts BusyBox init, reaches an
interactive shell, and reports Linux 7.1.0-i486 under qemu-pc98 and on a
physical PC-9821 Ra43. The i386 build has also booted on qemu-pc98 and
physical i386 PC-98 systems.

The BusyBox image includes `ip`, `ping`, and `udhcpc`. To obtain an address
on the first non-loopback interface and install the DHCP-provided route and
DNS configuration:

```sh
net-up
ip addr
```

The second-stage loader clears text and graphics VRAM, displays the kernel,
code, and data sizes, and updates the transferred code and data counts while
loading the non-compressed ELF `VMLINUX`. An optional 80 by 120 1bpp image is
drawn in white at the lower-right corner. The loader initializes the graphics
GDC and digital palette through the standard PC-98 BIOS interface, so this
screen works with both the NEC ROM BIOS and the compatible BIOS.

The i486 kernel includes the `e100` and `MII` drivers for the Ra43 onboard
PC-9821X-B06-compatible Intel PRO/100 adapter. Linux matches its primary
PCI ID `8086:1229`; the NEC subsystem ID `1033:8000` needs no separate
driver-table entry. The adapter has been detected on physical Ra43 hardware.

## Running under qemu-pc98

```sh
./build.sh run
```

Use `KERNEL_VERSION=7.1 ./build.sh run` for
`build/qemu-pc98-linux-7.1.raw`.

This image requires the PC-98-enabled qemu-pc98 build; upstream QEMU does not
provide the required machine and device implementations.

The official Debian 13 `i386` archive uses an i686/P6 baseline including
instructions such as `cmov`, so that image requires at least a Pentium II.
Disable the advertised CPU APIC feature with the current PC-98 machine:

```text
-cpu pentium2,-apic
```

A representative command line is:

```sh
qemu-system-i386 \
  -M pc9821 \
  -cpu pentium2,-apic \
  -m 128 \
  -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=build/qemu-pc98-linux.raw
```

`QEMU`, `BIOS_DIR`, `MACHINE`, `CPU`, `MEMORY`, `ACCEL`, and
`DISPLAY_BACKEND` can override the defaults used by `scripts/run-qemu.sh`.

For i486DX, Pentium, and Pentium MMX machines, use the project's Debian
13/i486DX image and package repository instead. Debian operation has a tested
minimum of 64 MiB RAM on both the i486DX and i686 paths.

## Console and framebuffer drivers

The boot console starts on the PC-98 GDC 80x25 text console. In a Debian/i686
installation on a confirmed Core-Graph machine, loading `pc98cirrusfb`
selects an 800x600x16 framebuffer by default and fbcon takes over.
`FRAMEBUFFER_CONSOLE_DETECT_PRIMARY` is disabled because the non-PnP Cirrus
child is not a conventional PCI VGA function. Japanese glyph support is not
required to reach the Debian direct shell.

The PC-98 framebuffer drivers are:

- `pc98cirrusfb` for the qemu-pc98 Core-Graph Cirrus GD5440; the Debian/i686
  configuration provides this as an explicitly loaded module, while i386
  and i486 set it to `n`
- `pc98tridentfb` for the integrated Trident TGUI9660/9680/9682 used by
  NEC Mate R systems; this remains an optional module

The driver is deliberately limited to the fixed-interface Core-Graph path;
PCI GD754x/755x laptop LCD support remains outside its scope. The 1 MiB
Core-Graph framebuffer supports exactly `640x480-24`,
`800x600-16`, and `1024x768-8`. The default prioritizes usable colour depth
for X11. The module exports no hardware alias and is not listed in any
modules-load configuration, so udev cannot select it automatically. On a
confirmed machine, load it immediately before X with
`modprobe pc98cirrusfb`; select another initial mode with
`modprobe pc98cirrusfb mode=<mode>`. An fbdev client can also request one of
these exact resolution/depth pairs with `FBIOPUT_VSCREENINFO`.

## Native input devices

The PC-98 keyboard driver already reports keys through the Linux input
subsystem. The normal i486/i686 Linux 7.1 profiles now enable `evdev`, so the
keyboard is available as `/dev/input/event0` as well as the console keyboard.
The native IRQ 13 PC-98 bus mouse is handled by the `pc98busmouse` module and
reports through both mousedev and evdev after `modprobe pc98busmouse`. In the
qemu-pc98 validation run these were `/dev/input/mouse0` and
`/dev/input/event1`; event numbers are not a stable ABI. It reports relative
X/Y and three buttons to the standard input API, so Xorg, gpm, SDL and other
non-X programs can use it without a special X-only driver.

The deliberately tiny 80386 profile does not enable evdev or the mouse
driver. In the normal build the linked `evdev` and `pc98busmouse` objects add
about 10 KiB of kernel text/data; no USB HID support is required for these
native devices.

The Trident driver is based on the register sequences documented in Suika3
`98disp_trident.c`. It handles the PC-98 BAR1 MMIO window, CR21 linear
aperture, display relay, and 640x480 8-bpp initialization. On Ra hardware,
long streams of direct aperture writes can be dropped during scanout. The
driver therefore exposes a system-RAM shadow framebuffer and copies changed
rows to VRAM with paced, read-back-verified writes.

`pc98tridentfb` deliberately has no PCI module alias, so Debian will not switch
away from the GDC console merely because udev discovers the Trident device.
Load it explicitly before starting an fbdev application or X server:

```sh
modprobe pc98tridentfb
```

Physical Ra43 testing still shows vertical colour bars after explicit module
loading. The remaining register-initialization mismatch is deferred; the GDC
console remains the supported display path for the current i486 work.

See `external/kernel/audit/` and the boot-chain sections above for further
implementation and validation details. The PC-98 machine model itself lives
in the `external/qemu-pc98` submodule.
