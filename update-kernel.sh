#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_version="${KERNEL_VERSION:-7.1}"
default_kernel_build="$repo/build/kernel-$kernel_version"
default_image="$repo/build/qemu-pc98-linux-$kernel_version.raw"
image="${1:-${OUTPUT_IMAGE:-$default_image}}"
kernel_image="${2:-${KERNEL_IMAGE:-${KERNEL_BUILD:-$default_kernel_build}/vmlinux.boot}}"

if [ ! -f "$image" ]; then
	echo "Disk image not found: $image" >&2
	exit 1
fi
if [ ! -f "$kernel_image" ]; then
	echo "Kernel image not found: $kernel_image" >&2
	exit 1
fi

make -C "$repo/loader"

logo_options=()
if [ -n "${BOOT_LOGO:-}" ]; then
	logo_options+=(--logo "$BOOT_LOGO")
fi

geometry_options=()
if [ -n "${DISK_HEADS:-}" ] || [ -n "${DISK_SECTORS:-}" ]; then
	if [ -z "${DISK_HEADS:-}" ] || [ -z "${DISK_SECTORS:-}" ]; then
		echo "DISK_HEADS and DISK_SECTORS must be set together" >&2
		exit 1
	fi
	geometry_options+=(--heads "$DISK_HEADS" --sectors "$DISK_SECTORS")
fi

python3 "$repo/tools/mk-pc98-linux-disk.py" update-kernel \
	"$image" \
	"$repo/loader/disk-ipl.bin" \
	"$repo/loader/partition-pbr.bin" \
	"$repo/loader/fat-loader.bin" \
	"$kernel_image" \
	"${geometry_options[@]}" \
	"${logo_options[@]}"
