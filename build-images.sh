#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
build="$repo/build"
kernel_version="${KERNEL_VERSION:-6.12}"
if [ "$kernel_version" = 6.12 ]; then
	default_kernel_build="$build/kernel"
	default_output="$build/qemu-pc98-linux.raw"
else
	default_kernel_build="$build/kernel-$kernel_version"
	default_output="$build/qemu-pc98-linux-$kernel_version.raw"
fi
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
kernel_image="${KERNEL_IMAGE:-$kernel_build/arch/x86/boot/bzImage}"
root_stage="${ROOT_STAGE:-$build/debian-i386-root}"
root_mb="${ROOT_MB:-1024}"
output="${OUTPUT_IMAGE:-$default_output}"

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

sudo python3 "$repo/tools/mk-pc98-linux-disk.py" create \
	"$output" \
	"$repo/loader/disk-ipl.bin" \
	"$repo/loader/partition-pbr.bin" \
	"$repo/loader/fat-loader.bin" \
	"$kernel_image" \
	"$root_stage" \
	--root-mb "$root_mb"
sudo chown "$(id -u):$(id -g)" "$output"

printf 'QEMU PC-98 Linux disk: %s\n' "$output"
