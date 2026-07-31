#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
cpu_family="${CPU_FAMILY:-486}"
case "$cpu_family" in
	386)
		cpu_name=i386
		default_kernel_build="$repo/build/i386-port/kernel"
		default_root_stage="$repo/build/i386-port/rootfs"
		default_output="$repo/build/i386-port/linux-7.1-pc98-i386-static-init.img"
		;;
	486)
		cpu_name=i486
		default_kernel_build="$repo/build/kernel-7.1-i486"
		default_root_stage="$repo/build/i486-rootfs"
		default_output="$repo/build/qemu-pc98-linux-7.1-i486.raw"
		;;
	686)
		cpu_name=i686
		default_kernel_build="$repo/build/kernel-7.1"
		default_root_stage="$repo/build/i686-rootfs"
		default_output="$repo/build/qemu-pc98-linux-7.1-i686-busybox.raw"
		;;
	*)
		echo "Unsupported CPU_FAMILY: $cpu_family (expected 386, 486, or 686)" >&2
		exit 1
		;;
esac
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
root_stage="${ROOT_STAGE:-$default_root_stage}"
output="${OUTPUT_IMAGE:-$default_output}"

if [ -e "$output" ]; then
	echo "Refusing to overwrite existing image: $output" >&2
	exit 1
fi

KERNEL_VERSION=7.1 \
CPU_FAMILY="$cpu_family" \
KERNEL_BUILD="$kernel_build" \
KERNEL_IMAGE="$kernel_build/arch/x86/boot/bzImage" \
ROOT_STAGE="$root_stage" \
ROOT_MB="${ROOT_MB:-64}" \
OUTPUT_IMAGE="$output" \
	"$repo/build-images.sh"
