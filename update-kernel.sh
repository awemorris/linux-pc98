#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_version="${KERNEL_VERSION:-6.12}"
if [ "$kernel_version" = 6.12 ]; then
	default_kernel_build="$repo/build/kernel"
	default_image="$repo/build/qemu-pc98-linux.raw"
else
	default_kernel_build="$repo/build/kernel-$kernel_version"
	default_image="$repo/build/qemu-pc98-linux-$kernel_version.raw"
fi
image="${1:-${OUTPUT_IMAGE:-$default_image}}"
kernel_image="${2:-${KERNEL_IMAGE:-${KERNEL_BUILD:-$default_kernel_build}/arch/x86/boot/bzImage}}"

if [ ! -f "$image" ]; then
	echo "Disk image not found: $image" >&2
	exit 1
fi
if [ ! -f "$kernel_image" ]; then
	echo "Kernel image not found: $kernel_image" >&2
	exit 1
fi

python3 "$repo/tools/mk-pc98-linux-disk.py" update-kernel \
	"$image" \
	"$kernel_image"
