#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_build="${KERNEL_BUILD:-$repo/build/kernel-7.1-i486}"
root_stage="${ROOT_STAGE:-$repo/build/i486-rootfs}"
output="${OUTPUT_IMAGE:-$repo/build/qemu-pc98-linux-7.1-i486.raw}"

if [ -e "$output" ]; then
	echo "Refusing to overwrite existing image: $output" >&2
	exit 1
fi

KERNEL_VERSION=7.1 \
KERNEL_BUILD="$kernel_build" \
KERNEL_IMAGE="$kernel_build/arch/x86/boot/bzImage" \
ROOT_STAGE="$root_stage" \
ROOT_MB="${ROOT_MB:-64}" \
OUTPUT_IMAGE="$output" \
	"$repo/build-images.sh"
