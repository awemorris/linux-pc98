# Linux 7.1 PC-98 Port Notes

## Baseline

`linux-7.1/` is based on the official Linux v7.1 source release:

- Source: <https://www.kernel.org/pub/linux/kernel/v7.x/linux-7.1.tar.xz>
- SHA-256: `691f44797fbe790dc8a321604c927087526ad27b6d649925d60f8eed0a2564a0`

The Linux 7.0 PC-98 change set was applied to a clean official v7.1 tree in
a temporary Git repository. This separates upstream v7.0-to-v7.1 changes
from the PC-98 port.

## Porting decisions

The 32-file Linux 7.0 PC-98 patch produced one merge conflict:

- `drivers/tty/serial/Kconfig`: Linux 7.1 no longer had the old ESP32 UART
  entries at the end of the menu. The v7.1 layout was retained and only the
  PC-98 uPD8251 entries were inserted.

Linux 7.1 changed the partition-parser diagnostic buffer from a character
array to `struct seq_buf`. The NEC98 parser therefore uses
`seq_buf_puts()` instead of `strlcat()`.

The resulting PC-98 delta relative to official v7.1 contains 32 files with
2,704 insertions and 4 deletions, plus the one-line `seq_buf` API update.

## Current scope

Linux 7.1 has two validated PC-98 targets:

- `CONFIG_M686=y` with a Debian 13 i386 userland
- `CONFIG_M486=y` with a minimal static musl/BusyBox userland

The i486 target is an experimental compatibility milestone rather than a
Debian Release image. The later i386 research target remains separate.

Linux removed `M486`, `M486SX`, and `MELAN` in upstream commit
`8b793a92d862c89055daa97ffa61a6929cf732f9`. The four affected x86
configuration files were restored from the parent of that commit. No
unrelated legacy device support was restored.

The default Linux 7.1 build uses `DEVICE_PROFILE=pc98`. The profile retains
the PCI core needed by the `pc9821` machine and the standard PCI UHCI, OHCI,
and EHCI USB host-controller drivers. Its fixed module allow-list keeps the
PC-98 Cirrus and Trident framebuffer modules plus generic USB HID,
mass-storage, CDC Ethernet/NCM, ACM serial, and printer support. It does not
derive the configuration from the build host's `lsmod` output.

The full Debian driver catalogue can still be selected explicitly with
`DEVICE_PROFILE=full`. The PC-98 profile reduces the number of configured
modular Kconfig entries from 3,644 to 23 (22 installed `.ko` files) and the
installed Linux 7.1 modules from about 190 MiB to about 1.8 MiB.

## Build

```sh
KERNEL_VERSION=7.1 ./build-kernel.sh
KERNEL_VERSION=7.1 ./build-images.sh
KERNEL_VERSION=7.1 ./run-qemu.sh
```

The generated raw image is `build/qemu-pc98-linux-7.1.raw`.

Build the experimental i486 kernel and its self-contained root filesystem
without Nix:

```sh
KERNEL_VERSION=7.1 \
CPU_FAMILY=486 \
KERNEL_BUILD=build/kernel-7.1-i486 \
LOCALVERSION=-i486 \
DEVICE_PROFILE=pc98 \
INSTALL_MODULES=0 \
  ./build-kernel.sh
./build-i486-rootfs.sh
./build-i486-image.sh
```

`build-i486-rootfs.sh` builds a freestanding `i486-linux-musl` GCC, static
musl, and static BusyBox. It compiles with `-march=i486` and rejects known
post-i486 instructions in the final BusyBox binary. Generated UAPI headers
use a separate output directory, so the Linux source tree remains clean.

The resulting image is
`build/qemu-pc98-linux-7.1-i486.raw`. Run it with:

```sh
qemu-system-i386 \
  -M pc9821 \
  -cpu 486 \
  -m 64 \
  -accel tcg \
  -drive if=ide,bus=0,unit=0,format=raw,file=build/qemu-pc98-linux-7.1-i486.raw
```

The PC-98 KASLR entropy path uses the native PIT counter and control ports
at `0x71` and `0x77`. This matters on an i486, where the absence of TSC makes
the early kernel fall back to the PIT; using the PC/AT ports `0x40` and
`0x43` caused an infinite early-boot loop.

The second-stage loader keeps the GDC text display responsive during the
relatively slow i486 bzImage load and decompression. Its first-row status
shows total size, physical load address, a live byte count, and progress
dots, followed by `Decompressing Linux...`. `earlyprintk=pc9800` then
displays kernel messages from time zero until the normal PC-98 console takes
over.

## Validation

- `CONFIG_M686=y` PC-98 `bzImage` build
- Complete full-profile module build before trimming
- Trimmed PC-98 profile build and `modules_install` (23 modular Kconfig
  entries, 22 installed `.ko` files)
- Cirrus and Trident PC-98 fbdev module builds
- Debian 13 two-partition raw image with only Linux 7.1 modules
- qemu-pc98 `pc9821` TCG boot with `-cpu pentium2,-apic`
- ext4 root mount, login prompt, and `uname -a` reporting Linux 7.1.0 i686
- `CONFIG_M486=y` kernel build with minimum CPU family 4
- Static i486 musl/BusyBox root filesystem with post-i486 instruction scan
- qemu-pc98 `pc9821` TCG boot with `-cpu 486`
- i486 ext4 root mount, BusyBox init and shell prompt
- `uname -a` reporting Linux 7.1.0-i486 on i486
- genuine PC-98 ROM boot through the disk IPL and partition PBR
- physical PC-9821 Ra43 boot through ext4 root to the i486 shell
- continuous loader, early-printk, and normal-console display handoff
- `git diff --check`

## Known limitation

The current built-in command line selects `root=/dev/sda2`. The genuine
fixed-disk IPL can select the Linux partition PBR on a second HDD and the
PBR successfully starts the kernel, but root mounting then fails because
the same filesystem is `/dev/sdb2`. Device-order-independent root selection
is intentionally left for a follow-up change.
