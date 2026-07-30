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

This first Linux 7.1 milestone is deliberately limited to the
Debian-oriented `CONFIG_M686=y` target. Restoring `CONFIG_M486` and
`CONFIG_M486SX`, adding a minimal i486-compatible root filesystem, and the
later i386 research target are separate follow-up changes.

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

## Validation

- `CONFIG_M686=y` PC-98 `bzImage` build
- Complete full-profile module build before trimming
- Trimmed PC-98 profile build and `modules_install` (23 modular Kconfig
  entries, 22 installed `.ko` files)
- Cirrus and Trident PC-98 fbdev module builds
- Debian 13 two-partition raw image with only Linux 7.1 modules
- qemu-pc98 `pc9821` TCG boot with `-cpu pentium2,-apic`
- ext4 root mount, login prompt, and `uname -a` reporting Linux 7.1.0 i686
- `git diff --check`
