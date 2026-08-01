Linux/pc98 Workspace
====================

This repository contains:

- Linux: 7.1/7.0/6.12, i386SX/DX and i486SX/DX port, with NEC PC-9800 machine support
- Debian: 13 "trixie", i486DX port, with NEC PC-9800 utils
- glibc: 2.41, i486DX port
- BusyBox: i386SX port
- qemu: 11.0, NEC PC-9800 support with compatible BIOS
- pc98boot: NEC PC-9800 HDD-IPL and FAT16 bootloader

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
variants require at least 64 MiB of physical RAM for a reliable login; the
smaller BusyBox systems are the supported choice below that threshold.

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
| `debian-i486/` | Framework and patch database for the Debian 13/i486DX package port |
| `configs/` | Debian-derived i686 base and versioned PC-98 configurations |
| `loader/` | PC-98 disk IPL and FAT16-aware Linux second-stage loader |
| `tools/` | Two-partition PC-98 raw disk image builder and helper tools |
| `build/` | Generated kernel, rootfs, logs, and disk images; ignored by Git |
| `build-kernel.sh` | Configures and builds the kernel and modules |
| `build-debian.sh` | Builds the Debian rootfs, kernel, modules, and disk image |
| `build-i386-rootfs.sh` | Builds the i386SX-compatible Linux 7.1 static-musl root filesystem |
| `build-i386-image.sh` | Builds the small Linux 7.1 i386 or i486 BusyBox image |
| `build-i486-rootfs.sh` | Builds a static i486 or i686 musl/BusyBox root filesystem without Nix |
| `build-i486-image.sh` | Builds a Linux 7.1/i486 or i686 BusyBox PC-98 disk image |
| `build-glibc.sh` | Builds and stages the maintained glibc 2.41 i486DX port or the exact-i386 research target |
| `build-glibc-validation-image.sh` | Builds a dynamic-glibc BusyBox validation disk |
| `test-glibc-qemu.sh` | Boots the validation disk under qemu-pc98 and checks its serial result |

Nix is not required. The build uses standard packages available on Debian 13.
See `toolchain/README.md` for the exact source baselines, local-source
integration, patch regeneration, update procedure, and automated patch
replay check.

The historical tree is reference material and is not used by the build.
`LINUX-6.12-PORT.md` records its exact provenance and maps the original
Linux/PC-98 implementation to the reconstructed Linux 6.12 platform and
drivers. The corresponding forward-port records are `LINUX-7.0-PORT.md` and
`LINUX-7.1-PORT.md`.

## glibc 2.41 ports

The supported Debian target is the glibc 2.41 i486DX port maintained in the
`toolchain/glibc` submodule. It uses the i486 native atomic instructions and
serves i486DX, Pentium, and Pentium MMX machines. An exact-i386 glibc research
target also passes its dedicated validation suite, but it is not a Debian
distribution target. It depends on a versioned kernel atomic syscall because
the original 80386 lacks `CMPXCHG` and `XADD`.

After the static-musl i386 Buildroot toolchain has been built, the complete
validation workflow is:

```sh
./build-glibc.sh i486
./build-glibc-tests.sh i486
./build-glibc-busybox.sh i486
./build-glibc-validation-image.sh i486
./test-glibc-qemu.sh i486
```

For exact-i386 research, replace `i486` with `i386` and additionally run
`check-glibc-i386-opcodes.sh`. The validation image runs a dynamically
glibc-linked BusyBox as `/sbin/init` and exercises malloc, dynamic loading,
TLS, pthreads, fork, and robust/process-shared mutexes. The i386-only path
also runs the kernel atomic suite.

See `GLIBC-2.41-I386-PORT-PLAN.md` for the implementation record, security
constraints and validation matrix for the exact-i386 research work. Debian
i486 packaging is tracked in `debian-i486/`.

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

On the first run, this script creates a rootfs from Debian trixie's official
`i386` archive in `build/debian-i386-root`. Despite the Debian architecture
name, these official binaries use an i686-class baseline and are intended for
Pentium II or newer systems. The script then builds Linux, installs the
modules, builds the PC-98 loaders, and creates:

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

The v0.4 Release set consists of the following persistent CF-card images.
The Debian images use the full PC-98 kernel configuration. The low-memory
Debian experiment is not published because login is not reliable below
64 MiB.

| Image | Userland | Minimum RAM |
| --- | --- | ---: |
| `linux-7.1-pc98-i386-busybox.img.xz` | static musl/BusyBox | 5 MiB |
| `linux-7.1-pc98-i486-busybox.img.xz` | static musl/BusyBox | 5 MiB |
| `debian13-pc98-i486-live-cfcard.img.xz` | Debian 13/i486 | 64 MiB |
| `debian13-pc98-i686-live-cfcard.img.xz` | Debian 13/i686 | 64 MiB |

Each archive has a matching `.sha256` file. Public images use the GDC screen
and PC-98 keyboard as the console.

## Disk layout

`build/qemu-pc98-linux.raw` is a single raw IDE disk with two native PC-98
partitions.

| Region | Contents |
| --- | --- |
| LBA 0 | Disk IPL with the `IPL1` marker |
| LBA 1 | PC-98 sixteen-entry partition table |
| LBA 2 through 135 | FAT16-aware Linux second-stage loader |
| Partition 1 | 200 MiB PC-98 DOS-compatible FAT16 containing non-compressed `VMLINUX` |
| Partition 2 | ext4 root filesystem, mounted as `/dev/sda2` |

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
| `BOOT_MB` | FAT16 boot partition size; default 200 MiB |
| `ROOT_MB` | ext4 root partition size; default 200 MiB |
| `BOOT_LOGO` | Optional 80 by 120 packed 1bpp logo for the boot screen |
| `DIST_IMAGE_NAME` | Filename inside `dist/` before the `.xz` suffix |

`build-debian-rootfs.sh` stops if `ROOT_STAGE` already exists, preventing an
existing rootfs from being overwritten accidentally.

Linux 7.0 supports both the official-Debian-oriented i686 kernel and the
PC-98 i486 kernel target. An i486 validation build can be made without
installing its modules into the official i686 Debian staging tree:

```sh
KERNEL_VERSION=7.0 CPU_FAMILY=486 INSTALL_MODULES=0 ./build-kernel.sh
```

Use the separately maintained Debian/i486DX port rather than the official
Debian archive on i486DX, Pentium, and Pentium MMX systems.

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

Linux 7.1 supports the PC-98 `CONFIG_M386=y`, `CONFIG_M486=y`, and
`CONFIG_M686=y` targets. The lower-generation work restores the x86 i386 and
i486 configurations removed by upstream Linux and supports i386SX, i386DX,
i486SX, and i486DX hardware. The small i386 and i486 release images use a
static musl/BusyBox userland; the i486DX release additionally provides the
custom Debian port.

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
  -m 5M \
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
./run-qemu.sh
```

Use `KERNEL_VERSION=7.1 ./run-qemu.sh` for
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
`DISPLAY_BACKEND` can override the defaults used by `run-qemu.sh`.

For i486DX, Pentium, and Pentium MMX machines, use the project's Debian
13/i486DX image and package repository instead. Debian operation has a tested
minimum of 64 MiB RAM on both the i486DX and i686 paths.

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
