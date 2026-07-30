qemu-pc98 test notes
====================

This tree was tested with the qemu-pc98 free compatibility BIOS on
`2026-07-30`.

## Important differences from NP2kai

The qemu-pc98 IDE option ROM reads LBA 0 and 1 and only executes an IPL when
bytes 4 through 7 contain `IPL1`.  `loader/disk-ipl.S` starts with:

```text
eb 06 90 90 49 50 4c 31
            I  P  L  1
```

The short jump preserves the entry point at offset zero, so the same IPL can
also be entered by firmware which does not inspect the marker.

The new image is a headerless raw disk.  LBA 1 carries a native PC-98
partition table using the BIOS geometry of 8 heads and 17 sectors.  Linux's
partition scanner includes `CONFIG_NEC98_PARTITION`, so it exposes these as
normal block-device partitions.

## Kernel

The PC-98 changes are integrated directly in `linux-6.12/`; it is not a
submodule and no patch-application step is required. Configure and build it
out of tree with:

```sh
./build-kernel.sh
```

The QEMU build uses Debian's non-PAE i686 distribution configuration as its
feature baseline, then overlays the PC-98 platform, built-in boot devices,
ext4 root support and the PC-98 console. Build products are kept under
`build/kernel`, leaving `linux-6.12/` as a source-only directory. The boot disk is
sized from the resulting `bzImage`; it is not constrained to floppy capacity.

## Debian i386 root

The tested root is a Debian i386 minbase system using Debian's packaged
SysV init. BusyBox is not installed and is not involved in boot:

```sh
./build-debian-rootfs.sh
```

Build the QEMU disk:

```sh
./build-images.sh
```

The image uses one IDE disk with two PC-98 partitions. The first is a normal
DOS-compatible FAT16 volume containing `BZIMAGE`; the second is an ext4
filesystem mounted as `/dev/sda2`. LBA 2 through 135, before the first
partition, contain the FAT-aware second-stage loader. No special PBR is
required, so DOS may install its own PBR without disturbing the Linux IPL
path. The root image boots `/sbin/init` from Debian's `sysvinit-core` package
and provides login prompts on both the PC-98 console and `ttyS0`.
The initial root password in this local emulator image is `pc98`.
The default root image size is 1 GiB so the distribution-style kernel modules
fit; override it with `ROOT_MB` when generating an image.

Update only the FAT16 kernel partition, preserving the ext4 userland:

```sh
./update-kernel.sh
```

## Run

```sh
./run-qemu.sh
```

Debian 13 i386 uses P6 instructions such as `cmov`.  The qemu-pc98 `pc9801`
default CPU is a 486, so the Debian root requires at least `-cpu pentium2`.
The Pentium II CPU model advertises APIC, while `pc9801` has no APIC wiring;
`-apic` must therefore be removed from the CPU feature set:

```text
-cpu pentium2,-apic
```

## Display consoles

The normal boot console remains the PC-98 GDC 80x25 text console. Japanese
glyph support is not required for booting the Debian system.

The Cirrus framebuffer is a loadable module intended for a later X11
session, rather than the kernel boot console. It performs the PC-98
Core-Graph routing and Cirrus initialization when loaded:

```sh
modprobe pc98cirrusfb
cat /proc/fb
```

It registers `/dev/fb0` as a 640x480 8-bpp linear framebuffer. Do not add the
module to the early boot module list when the GDC text console is desired.

The tree also contains `pc98tridentfb`, an initial real-hardware driver for
the built-in Trident TGUI9660/9680/9682 on NEC Mate R machines such as the
Ra43, Ra33, Ra266 and Ra300. It is derived from the field notes and register
sequences in Suika3's `98disp_trident.c`, and handles both native VGA PIO and
the Ra-generation BAR1 MMIO register window:

```sh
modprobe pc98tridentfb
cat /proc/fb
```

The generic Linux `tridentfb` driver is disabled because it binds the same
PCI ID without performing the PC-98 display-relay and aperture setup. The
PC-98 driver currently exposes only 640x480 8-bpp with a 1024-byte pitch.
It has passed compilation and static checks, but has not yet been exercised
on a physical Mate R. In particular, direct writes to the Ra framebuffer
window may be dropped by the hardware; a graphics-engine or verified shadow
copy path remains a prerequisite for claiming reliable X11 operation.

Normally the driver derives the linear aperture from CR21 (or BAR0 on
PIO-generation machines). `fb_phys=0x...` overrides that address for
diagnostics. The potentially unsafe PC-98 wake-up sequence using port 0x94
is disabled by default and must be explicitly enabled with
`allow_pc98_wakeup=1`.

For an i486-capable physical PC-98, use the repository's musl-based i486
BusyBox build instead of the Debian 13 userspace.

## Verified result

The free compatibility BIOS loaded the `IPL1` disk, the LBA 2 second stage
found `BZIMAGE` through its FAT16 cluster chain, and Linux 6.12 parsed both
PC-98 partitions. It mounted ext4 from `/dev/sda2` and reached the Debian 13
`pc98 login:` prompt under qemu-pc98 TCG.
The following commands completed using Debian's i386 userspace:

```text
Linux pc98 6.12.0+ ... i686 GNU/Linux
13.6
```

The calendar reader explicitly selects the uPD4990A 48-bit format before
latching it. This avoids inheriting the uPD4993 extended format from firmware,
which previously shifted all fields by a nibble, produced a year in 2067 and
made 32-bit Debian startup utilities fail after the 2038 boundary.

The PC-9800 keyboard driver also initializes its uPD8251 and receives
scancodes on IRQ1. It no longer depends on firmware state or a 10 ms polling
timer. A login through the GDC console was verified with the interrupt-driven
driver.
