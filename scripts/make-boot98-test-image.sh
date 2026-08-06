#!/usr/bin/env bash

set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader_dir="$repo/bootloader"
output="${1:-$repo/build/boot98/boot98-partition-ipl-test.img}"

if [ -e "$output" ]; then
    echo "Refusing to overwrite existing image: $output" >&2;
    exit 1;
fi

make -C "$bootloader_dir" ipl-lba0.bin ipl-lba2.bin boot.sys boot.bin

size="$(stat -c%s "$bootloader_dir/boot.sys")"

test "$size" -le 7168

mkdir -p "$(dirname "$output")"

truncate -s "${BOOT98_TEST_MB:-16}M" "$output"

dd if="$bootloader_dir/ipl-lba0.bin" of="$output" bs=512 count=1 \
	conv=notrunc status=none
dd if="$bootloader_dir/ipl-lba2.bin" of="$output" bs=512 seek=2 count=14 \
	conv=notrunc status=none

printf '\241\040\000\000\000\000\020\000\003\002\020\000\020\007\177\000BOOT            ' | \
	dd of="$output" bs=1 seek=512 count=32 conv=notrunc status=none

dd if="$bootloader_dir/boot.sys" of="$output" bs=512 \
	seek=2176 conv=notrunc status=none

stage2="${BOOT98_STAGE2:-$bootloader_dir/boot.bin}"
if [ -n "$stage2" ]; then
	partition_offset=$((2213 * 512))
	partition_sectors=$((17408 - 2213))

	mformat -i "$output@@$partition_offset" -c 1 -h 8 -s 17 \
		-T "$partition_sectors" -v BOOT ::

	mcopy -i "$output@@$partition_offset" "$stage2" ::BOOT.BIN

	if [ -n "${BOOT98_CFG:-}" ]; then
		mcopy -i "$output@@$partition_offset" "$BOOT98_CFG" ::BOOT.CFG
	fi

	if [ -n "${BOOT98_KERNEL:-}" ]; then
		mcopy -i "$output@@$partition_offset" "$BOOT98_KERNEL" ::VMLINUX
	fi

	if [ -n "${BOOT98_IPLWARE_BIN:-}" ]; then
		mcopy -i "$output@@$partition_offset" "$BOOT98_IPLWARE_BIN" ::IPLTEST.BIN
	fi

	if [ -n "${BOOT98_IPLWARE_COM:-}" ]; then
		mcopy -i "$output@@$partition_offset" "$BOOT98_IPLWARE_COM" ::IPLCOM.COM
	fi

	if [ -n "${BOOT98_APPLET:-}" ]; then
		mcopy -i "$output@@$partition_offset" "$BOOT98_APPLET" ::BOOTAPP.BIN
	fi

	if [ -n "${BOOT98_FILES:-}" ]; then
		for file in "$BOOT98_FILES"/*; do
			[ -f "$file" ] || continue
			mcopy -i "$output@@$partition_offset" "$file" ::
		done
	fi
fi

sha256sum "$output"

printf 'BOOT98 test image: %s\n' "$output"
printf 'boot.sys: %s bytes at the BOOT partition IPL (maximum 7168)\n' "$size"
