#!/usr/bin/env bash

set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader_dir="$repo/bootloader"
output="${1:-$repo/build/boot98/boot98-fdd-test.img}"

if [ -e "$output" ]; then
	echo "Refusing to overwrite existing image: $output" >&2
	exit 1
fi

make -C "$bootloader_dir" boot98-fdd-ipl.bin boot98-stage1.bin

mkdir -p "$(dirname "$output")"

truncate -s 1440K "$output"

dd if="$bootloader_dir/boot98-fdd-ipl.bin" of="$output" bs=512 \
	count=1 conv=notrunc status=none
dd if="$bootloader_dir/boot98-stage1.bin" of="$output" bs=512 \
	seek=2 conv=notrunc status=none

sha256sum "$output"

printf 'BOOT98 FDD test image: %s\n' "$output"
