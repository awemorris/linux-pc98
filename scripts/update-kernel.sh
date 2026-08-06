#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
kernel_version="${KERNEL_VERSION:-7.1}"
default_kernel_build="$repo/build/kernel-$kernel_version"
default_image="$repo/build/qemu-pc98-linux-$kernel_version.raw"
image="${1:-${OUTPUT_IMAGE:-$default_image}}"
kernel_image="${2:-${KERNEL_IMAGE:-${KERNEL_BUILD:-$default_kernel_build}/vmlinux.boot}}"
dos_loader="${DOS_LOADER:-$repo/bootloader/dos/linux98.exe}"
boot_cfg="${BOOT_CFG:-$repo/releases/boot98.cfg}"

if [ ! -f "$image" ]; then
	echo "Disk image not found: $image" >&2
	exit 1
fi
if [ ! -f "$kernel_image" ]; then
	echo "Kernel image not found: $kernel_image" >&2
	exit 1
fi

make -C "$repo/bootloader"
if [ ! -f "$dos_loader" ]; then
	echo "DOS Linux loader not found: $dos_loader" >&2
	echo "Restore the tracked binary or run ./build.sh dos-loader." >&2
	exit 1
fi

# Recreate only the BOOT filesystem and install the matching partition IPL,
# boot.bin, kernel, and DOS loader.  Root and swap partitions are untouched.
exec "$repo/scripts/install-boot98-image.sh" "$image" "$kernel_image" "$boot_cfg"
