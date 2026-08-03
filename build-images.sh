#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
build="$repo/build"
kernel_version="${KERNEL_VERSION:-7.1}"
default_kernel_build="$build/kernel-$kernel_version"
default_output="$build/qemu-pc98-linux-$kernel_version.raw"
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
kernel_image="${KERNEL_IMAGE:-$kernel_build/vmlinux.boot}"
root_stage="${ROOT_STAGE:-$build/debian-i386-root}"
boot_mb="${BOOT_MB:-200}"
root_mb="${ROOT_MB:-200}"
swap_mb="${SWAP_MB:-0}"
small_ext4="${SMALL_EXT4:-0}"
disk_heads="${DISK_HEADS:-8}"
disk_sectors="${DISK_SECTORS:-17}"
output="${OUTPUT_IMAGE:-$default_output}"
dos_loader="${DOS_LOADER:-$repo/loader/dos/linux98.exe}"

if [ ! -f "$kernel_image" ]; then
	echo "Kernel image not found: $kernel_image" >&2
	echo "Run ./build-kernel.sh first." >&2
	exit 1
fi
if [ ! -d "$root_stage" ]; then
	echo "Rootfs staging tree not found: $root_stage" >&2
	echo "Run ./build-debian.sh (or ./build-debian-rootfs.sh) first." >&2
	exit 1
fi

mkdir -p "$build"
make -C "$repo/loader"
make -C "$repo/loader/dos"
if [ ! -f "$dos_loader" ]; then
	echo "DOS Linux loader not found: $dos_loader" >&2
	exit 1
fi

image_options=(
	--boot-mb "$boot_mb"
	--root-mb "$root_mb"
	--swap-mb "$swap_mb"
	--heads "$disk_heads"
	--sectors "$disk_sectors"
	--dos-loader "$dos_loader"
)
if [ "$small_ext4" != 0 ]; then
	image_options+=(--small-ext4)
fi
if [ -n "${BOOT_LOGO:-}" ]; then
	image_options+=(--logo "$BOOT_LOGO")
fi

sudo python3 "$repo/tools/mk-pc98-linux-disk.py" create \
	"$output" \
	"$repo/loader/disk-ipl.bin" \
	"$repo/loader/partition-pbr.bin" \
	"$repo/loader/fat-loader.bin" \
	"$kernel_image" \
	"$root_stage" \
	"${image_options[@]}"
sudo chown "$(id -u):$(id -g)" "$output"

printf 'QEMU PC-98 Linux disk: %s\n' "$output"
