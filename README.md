Linux/PC-98 for QEMU PC98 and Mirai98
=====================================

This repository contains a Linux 6.12 port for the NEC PC-9800 series,
boot loaders, tools for building a Debian i386 userland, and a raw disk image
builder for qemu-pc98.

## Repository layout

| Path              | Contents                                                         |
|-------------------|------------------------------------------------------------------|
| `linux-6.12/`     | Linux 6.12 source tree with the PC-98 port integrated            |
| `configs/`        | Debian-derived i686 base configuration and PC-98 configuration   |
| `loader/`         | PC-98 disk IPL and FAT16-aware Linux second-stage loader         |
| `tools/`          | Two-partition PC-98 raw disk image builder and helper tools      |
| `build/`          | Generated kernel, rootfs, logs, and disk images; ignored by Git  |
| `build-kernel.sh` | Configures and builds the kernel and modules                     |
| `build-debian.sh` | Builds the Debian rootfs, kernel, modules, and disk image        |

## Host requirements

Install the normal Linux kernel build dependencies together with
`debootstrap`, `e2fsprogs`, `sudo`, Python 3, and binutils capable of
producing 32-bit ELF files. A typical Debian 13 host can use:

```sh
sudo apt install \
  build-essential bc bison flex libssl-dev libelf-dev dwarves \
  debootstrap e2fsprogs sudo python3 binutils xz-utils mtools
```

Root privileges are used when creating the Debian staging tree, installing
kernel modules into it, and generating the ext4 filesystem image.

## Complete Debian image build

```sh
./build-debian.sh
```

On the first run, this script creates a Debian trixie i386 minbase rootfs in
`build/debian-i386-root`. It then builds Linux, installs the modules, builds
the PC-98 loaders, and creates:

```text
build/qemu-pc98-linux.raw
```

The kernel is built out of tree in `build/kernel`; `linux-6.12/` remains a
source-only directory.

The individual build stages can also be run separately:

```sh
./build-debian-rootfs.sh
./build-kernel.sh
./build-images.sh
```

## Disk layout

`build/qemu-pc98-linux.raw` is a single raw IDE disk with two native PC-98
partitions.

| Region      | Contents                                                  |
|-------------|-----------------------------------------------------------|
| LBA 0       | Disk IPL with the `IPL1` marker                           |
| LBA 1       | PC-98 sixteen-entry partition table                       |
| LBA 2-135   | FAT16-aware Linux second-stage loader                     |
| Partition 1 | Standard DOS-compatible FAT16 containing `BZIMAGE`        |
| Partition 2 | Debian i386 ext4 root filesystem, mounted as `/dev/sda2`  |

Partition 1 does not use a custom PBR. The LBA 0 IPL loads the Linux loader
directly from LBA 2, so DOS may install its own PBR without affecting the
Linux boot path. If DOS reformats the whole FAT16 partition, copy `BZIMAGE`
back afterwards.

The Linux port includes a PC-98 partition-table parser so both partitions are
reported through the normal Linux block-device interface.

## Updating only the kernel

To update `BZIMAGE` in Partition 1 without rebuilding or modifying the
Debian userland in Partition 2:

```sh
./update-kernel.sh
```

An alternate raw image and bzImage can be supplied as arguments:

```sh
./update-kernel.sh path/to/disk.raw path/to/bzImage
```

## Build configuration

The main environment-variable overrides are:

| Variable         | Default or purpose                                  |
|------------------|-----------------------------------------------------|
| `JOBS            | `nproc`; parallel kernel build job count            |
| `KERNEL_BUILD`   | `build/kernel`; out-of-tree kernel build directory  |
| `ROOT_STAGE`     | `build/debian-i386-root`                            |
| `DEBIAN_SUITE`   | `trixie`                                            |
| `DEBIAN_MIRROR`  | Official Debian mirror                              |
| `DEBIAN_INCLUDE` | Comma-separated packages added to the rootfs        |
| `ROOT_PASSWORD`  | Initial local test password; default `pc98`         |
| `ROOT_MB`        | ext4 root partition size; default 1024 MiB          |

`build-debian-rootfs.sh` stops if `ROOT_STAGE` already exists, preventing an
existing rootfs from being overwritten accidentally.

## Running under qemu-pc98

```sh
./run-qemu.sh
```

This image requires the PC-98-enabled qemu-pc98 build; upstream QEMU does not
provide the required machine and device implementations.

Debian 13 i386 uses P6 instructions such as `cmov`, so the guest CPU must be
at least a Pentium II. The PC-98 machine does not currently connect the APIC,
therefore disable the advertised CPU APIC feature:

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
`DISPLAY_BACKEND` can override the defaults used by `run-qemu.sh`.

The Debian 13 i386 userland is not suitable for an i486-only physical PC-98.
Such hardware requires a separately built i486-compatible userland.

## Console and framebuffer drivers

The boot console is the PC-98 GDC 80x25 text console. Japanese glyph support
is not required to reach the Debian login prompt.

Two optional fbdev modules are included for later graphical use:

- `pc98cirrusfb` for the qemu-pc98 Core-Graph Cirrus GD5440
- `pc98tridentfb` for the integrated Trident TGUI9660/9680/9682 used by
  NEC Mate R systems

The Trident driver is based on the register sequences documented in Suika3
`98disp_trident.c`. It handles the PC-98 BAR1 MMIO window, CR21 linear
aperture, display relay, and 640x480 8-bpp initialization. It compiles and
passes static checks but has not yet been tested on physical Mate R hardware.

See `QEMU-PC98.md`, `LINUX-PC98-AUDIT.md`, and `loader/README.md` for further
implementation and validation details.
