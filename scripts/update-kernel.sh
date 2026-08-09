#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
. "$repo/scripts/boots-env.sh"
kernel_version="${KERNEL_VERSION:-7.1}"
default_kernel_build="$repo/build/kernel-$kernel_version"
default_image="$repo/build/qemu-pc98-linux-$kernel_version.raw"
image="${1:-${OUTPUT_IMAGE:-$default_image}}"
kernel_image="${2:-${KERNEL_IMAGE:-${KERNEL_BUILD:-$default_kernel_build}/vmlinux.boot}}"
dos_loader="${DOS_LOADER:-$repo/external/boots/platform/pc98/dos/linux98.exe}"
boot_cfg="${BOOT_CFG:-$repo/configs/boots.cfg}"

if [ ! -f "$image" ]; then
	echo "Disk image not found: $image" >&2
	exit 1
fi
if [ ! -f "$kernel_image" ]; then
	echo "Kernel image not found: $kernel_image" >&2
	exit 1
fi

"$repo/external/boots/build.sh" pc98
if [ ! -f "$dos_loader" ]; then
	echo "DOS Linux loader not found: $dos_loader" >&2
	echo "Restore the tracked binary or run ./build.sh dos-loader." >&2
	exit 1
fi

# Recreate only the BOOT filesystem and install the matching partition IPL,
# BOOT.SYS, kernel, and DOS loader.  Root and swap partitions are untouched.
exec "$repo/external/boots/scripts/install-image.sh" "$image" "$kernel_image" "$boot_cfg"
