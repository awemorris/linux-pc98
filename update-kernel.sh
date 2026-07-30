#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
image="${1:-$repo/build/qemu-pc98-linux.raw}"
kernel_image="${2:-$repo/build/kernel/arch/x86/boot/bzImage}"

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
