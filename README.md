Linux/pc98 Workspace
====================

This repository contains Linux 6.12, Linux 7.0, and Linux 7.1 ports for the
NEC PC-9800 series, boot loaders, tools for building a Debian i386 userland,
and a raw disk image builder for qemu-pc98.

Clone it with its maintained toolchain and emulator sources:

```sh
git clone --recurse-submodules https://github.com/awemorris/linux-pc98.git
```

For an existing checkout, initialize the same sources with:

```sh
git submodule update --init --recursive
```

## Repository layout

| Path | Contents |
| --- | --- |
| `linux-2.6.7-pc98-original/` | Immutable last-complete upstream PC-9800 source snapshot from immediately before its 2004 removal |
| `linux-6.12/` | Linux 6.12 source tree with the PC-98 port integrated |
| `linux-7.0/` | Linux 7.0 source tree with the PC-98 port forward-ported |
| `linux-7.1/` | Linux 7.1 source tree with the PC-98 port forward-ported |
| `patches/linux-6.12-pc98/` | Recovered chronological 6.12 series and the complete current v6.12 delta |
| `qemu-pc98/` | qemu-pc98 submodule used for i386 and PC-98 validation |
| `toolchain/` | GCC, musl, and glibc submodules plus the versioned patch inventory |
| `configs/` | Debian-derived i686 base and versioned PC-98 configurations |
| `loader/` | PC-98 disk IPL and FAT16-aware Linux second-stage loader |
| `tools/` | Two-partition PC-98 raw disk image builder and helper tools |
| `build/` | Generated kernel, rootfs, logs, and disk images; ignored by Git |
| `build-kernel.sh` | Configures and builds the kernel and modules |
| `build-debian.sh` | Builds the Debian rootfs, kernel, modules, and disk image |
| `build-i386-rootfs.sh` | Builds the experimental Linux 7.1/i386 static-musl root filesystem |
| `build-i386-image.sh` | Builds the small Linux 7.1 i386 or i486 BusyBox image |
| `build-i486-rootfs.sh` | Builds a static i486 or i686 musl/BusyBox root filesystem without Nix |
| `build-i486-image.sh` | Builds a Linux 7.1/i486 or i686 BusyBox PC-98 disk image |

Nix is not required. The build uses standard packages available on Debian 13.
See `toolchain/README.md` for the exact source baselines, local-source
integration, patch regeneration, update procedure, and automated patch
replay check.

The historical tree is reference material and is not used by the build.
`LINUX-6.12-PORT.md` records its exact provenance and maps the original
Linux/PC-98 implementation to the reconstructed Linux 6.12 platform and
drivers. The corresponding forward-port records are `LINUX-7.0-PORT.md` and
`LINUX-7.1-PORT.md`.

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

Linux 6.12 remains the default so existing build and release procedures keep
their original behavior:

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

To build a versioned Linux image instead:

```sh
KERNEL_VERSION=7.1 ./build-debian.sh
```

This uses `linux-7.1/`, builds out of tree in `build/kernel-7.1`, and writes
`build/qemu-pc98-linux-7.1.raw`. It does not overwrite the Linux 6.12 kernel
or disk image.

The individual build stages can also be run separately:

```sh
./build-debian-rootfs.sh
./build-kernel.sh
./build-images.sh
```

To create an xz-compressed Release artifact and its SHA-256 file:

```sh
KERNEL_VERSION=7.1 make dist
```

The Linux 7.1 artifact is named
`dist/qemu-pc98-debian13-i386-linux-7.1.raw.xz`. `DIST_BASENAME`,
`XZ_LEVEL`, and `XZ_THREADS` can override the name and compression settings.
Existing dist files are never overwritten.

## Disk layout

`build/qemu-pc98-linux.raw` is a single raw IDE disk with two native PC-98
partitions.

| Region | Contents |
| --- | --- |
| LBA 0 | Disk IPL with the `IPL1` marker |
| LBA 1 | PC-98 sixteen-entry partition table |
| LBA 2 through 135 | FAT16-aware Linux second-stage loader |
| Partition 1 | 200 MiB PC-98 DOS-compatible FAT16 containing non-compressed `VMLINUX` |
| Partition 2 | Debian i386 ext4 root filesystem, mounted as `/dev/sda2` |

The LBA 0 IPL loads the Linux loader directly from LBA 2. Partition 1 also
has a free PC-98 FAT16 PBR, so a genuine PC-98 disk IPL can boot Linux after
the user selects the active Linux partition. The BPB uses the NEC fixed-disk
convention of 1024-byte logical DOS sectors and includes the PC-98 extension
at offsets `0x3e..0x45`.

The current built-in kernel command line uses `root=/dev/sda2`. A genuine
IPL can select Partition 1 from a second HDD and successfully enter the
Linux kernel, but the kernel then looks for the root filesystem on the first
HDD and panics. Device-order-independent root selection remains follow-up
work; use this image as the first HDD for a complete boot.

DOS may install its own PBR and filesystem in Partition 1. Reformatting it
removes `VMLINUX`, so restore the kernel afterwards if the partition is to
remain Linux-bootable.

The Linux port includes a PC-98 partition-table parser so both partitions are
reported through the normal Linux block-device interface.

## Updating only the kernel

To update `VMLINUX` in Partition 1 without rebuilding or modifying the
Debian userland in Partition 2:

```sh
./update-kernel.sh
```

An alternate raw image and stripped, non-compressed ELF vmlinux can be supplied as arguments:

```sh
./update-kernel.sh path/to/disk.raw path/to/vmlinux.boot
```

## Build configuration

The main environment-variable overrides are:

| Variable | Default or purpose |
| --- | --- |
| `KERNEL_VERSION` | `6.12`; select `6.12`, `7.0`, or `7.1` |
| `KERNEL_SOURCE` | `linux-$KERNEL_VERSION` |
| `JOBS` | `nproc`; parallel kernel build job count |
| `KERNEL_BUILD` | `build/kernel` for 6.12; otherwise `build/kernel-$KERNEL_VERSION` |
| `CPU_FAMILY` | `686`; Linux 7.0 and 7.1 also retain the experimental `486` target |
| `DEVICE_PROFILE` | Linux 7.1 defaults to `pc98`; use `full` for the full Debian driver catalogue |
| `CONSOLE_MODE` | `video`; use `dual` only for a private GDC plus serial diagnostic build |
| `INSTALL_MODULES` | `1`; set to `0` to skip `modules_install` |
| `OUTPUT_IMAGE` | Version-specific raw image path |
| `ROOT_STAGE` | `build/debian-i386-root` |
| `DEBIAN_SUITE` | `trixie` |
| `DEBIAN_MIRROR` | Official Debian mirror |
| `DEBIAN_INCLUDE` | Comma-separated packages added to the rootfs |
| `ROOT_PASSWORD` | Initial local test password; default `pc98` |
| `BOOT_MB` | FAT16 boot partition size; default 200 MiB |
| `ROOT_MB` | ext4 root partition size; default 200 MiB |
| `DIST_IMAGE_NAME` | Filename inside `dist/` before the `.xz` suffix |

`build-debian-rootfs.sh` stops if `ROOT_STAGE` already exists, preventing an
existing rootfs from being overwritten accidentally.

Linux 7.0 supports both the Debian-oriented i686 kernel and the retained i486
kernel target. An i486 validation build can be made without installing its
modules into the Debian staging tree:

```sh
KERNEL_VERSION=7.0 CPU_FAMILY=486 INSTALL_MODULES=0 ./build-kernel.sh
```

The Debian 13 userland remains i686 and therefore is not usable on an actual
i486 even when the kernel itself is built for i486.

Linux 7.1 uses the `pc98` device profile by default. It retains the PCI core
required by `pc9821`, the PC-98 IDE and framebuffer drivers, and standard
USB 1.x/2.0 UHCI/OHCI/EHCI host controllers. The USB module set is limited
to generic HID, mass-storage, CDC Ethernet/NCM, ACM serial, and printer
classes. The fixed module allow-list is stored in
`configs/pc9800-modules.list`; it does not depend on the build host's loaded
modules. This reduces the configured module count from 3,644 in the full
Debian configuration to 23 modular Kconfig entries (22 installed `.ko`
files).

The untrimmed Debian driver catalogue remains available for comparison:

```sh
KERNEL_VERSION=7.1 DEVICE_PROFILE=full ./build-kernel.sh
```

## Linux 7.0 port status

The `linux-7.0/` tree is based on the official Linux v7.0 release. The PC-98
platform, partition parser, PATA, keyboard, serial, text console, Cirrus
fbdev, and Trident fbdev changes were forward-ported from this repository's
6.12 tree. The serial driver uses the Linux 7 timer API
(`timer_delete_sync()`).

Both `CONFIG_M686=y` and `CONFIG_M486=y` PC-98 kernels build successfully.
The complete Debian-oriented module set also builds and installs. The i686
kernel has booted under qemu-pc98 through the custom IPL and FAT16 loader,
mounted the Debian ext4 root filesystem, reached the login prompt, and
reported Linux 7.0.0 from `uname`.

## Linux 7.1 port status

The `linux-7.1/` tree is based on the official Linux v7.1 release. The PC-98
platform and device changes were forward-ported from the Linux 7.0 tree.
Linux 7.1 changed partition-parser logging to `struct seq_buf`; the NEC98
parser follows the new API.

Linux 7.1 supports the Debian-oriented `CONFIG_M686=y` target and an
experimental `CONFIG_M486=y` target. The latter restores the x86 i486
configuration removed by upstream Linux 7.1 and uses a separately built,
static musl/BusyBox root filesystem instead of the i686 Debian userland.

The trimmed kernel and module set build successfully. The generated
two-partition image boots under qemu-pc98 with TCG, mounts the Debian 13 ext4
root filesystem, reaches the login prompt, and reports Linux 7.1.0 i686.

To build and run the small i486 image with the same PC-98-only profile and
i386-compatible BusyBox used by the genuine-i386 image:

```sh
CPU_FAMILY=486 I386_CONSOLE=video ./build-i386-image.sh

qemu-system-i386 \
  -M pc9801 \
  -cpu 486 \
  -m 16 \
  -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=build/i486-video/linux-7.1-pc98-i486-busybox.img
```

The larger generic scripts can still build a Linux 7.1/i686 BusyBox
development image, but Release images use the Debian 13 userland for i686:

```sh
CPU_FAMILY=686 ./build-i486-rootfs.sh
CPU_FAMILY=686 ./build-i486-image.sh

qemu-system-i386 \
  -M pc9821 \
  -cpu pentium2,-apic \
  -m 64 \
  -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=build/qemu-pc98-linux-7.1-i686-busybox.raw
```

The equivalent convenience targets are `make busybox-i486` and
`make busybox-i686`. Set `BUSYBOX_WORK` or `ROOT_STAGE` to override their
default build and staging directories.

The i486 image mounts its ext4 root, starts BusyBox init, reaches an
interactive shell, and reports Linux 7.1.0-i486 under qemu-pc98 and on a
physical PC-9821 Ra43. The i386 target remains a research milestone.

The BusyBox image includes `ip`, `ping`, and `udhcpc`. To obtain an address
on the first non-loopback interface and install the DHCP-provided route and
DNS configuration:

```sh
net-up
ip addr
```

During the slower i486 kernel load and decompression, the second-stage loader
shows a BSD-style size, load address, loaded-byte count, and progress dots on
the first GDC text row. The kernel enables the PC-98 early console immediately
after decompression, so the display no longer remains black between the
firmware boot menu and normal console initialization.

The i486 kernel includes the `e100` and `MII` drivers for the Ra43 onboard
PC-9821X-B06-compatible Intel PRO/100 adapter. Linux matches its primary
PCI ID `8086:1229`; the NEC subsystem ID `1033:8000` needs no separate
driver-table entry. The adapter has been detected on physical Ra43 hardware.

## Running under qemu-pc98

```sh
./run-qemu.sh
```

Use `KERNEL_VERSION=7.1 ./run-qemu.sh` for
`build/qemu-pc98-linux-7.1.raw`.

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

See `LINUX-7.0-PORT.md`, `LINUX-7.1-PORT.md`, `QEMU-PC98.md`, and
`loader/README.md` for further implementation and validation details.
