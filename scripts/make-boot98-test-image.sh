#!/usr/bin/env bash

set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader_dir="$repo/bootloader"
output="${1:-$repo/build/boot98/boot98-stage1-test.img}"

if [ -e "$output" ]; then
    echo "Refusing to overwrite existing image: $output" >&2;
    exit 1;
fi

make -C "$bootloader_dir" disk-ipl.bin boot98-stage1.bin \
	boot98-chain-test.bin

size="$(stat -c%s "$bootloader_dir/boot98-stage1.bin")"

test "$size" -le 7168

mkdir -p "$(dirname "$output")"

truncate -s "${BOOT98_TEST_MB:-16}M" "$output"

dd if="$bootloader_dir/disk-ipl.bin" of="$output" bs=512 count=1 \
	conv=notrunc status=none
dd if="$bootloader_dir/boot98-stage1.bin" of="$output" bs=512 seek=2 \
	conv=notrunc status=none

printf '\016\000' | dd of="$output" bs=1 seek=496 count=2 conv=notrunc status=none
printf '\241\040\000\000\000\000\020\000\003\002\020\000\020\007\177\000BOOT            ' | \
	dd of="$output" bs=1 seek=512 count=32 conv=notrunc status=none

dd if="$bootloader_dir/boot98-chain-test.bin" of="$output" bs=512 \
	seek=2176 count=1 conv=notrunc status=none

if [ -n "${BOOT98_STAGE2:-}" ]; then
	partition_offset=$((2213 * 512))
	partition_sectors=$((17408 - 2213))

	mformat -i "$output@@$partition_offset" -c 1 -h 8 -s 17 \
		-T "$partition_sectors" -v BOOT ::

	mcopy -i "$output@@$partition_offset" "$BOOT98_STAGE2" ::BOOT98.BIN

	if [ -n "${BOOT98_CFG:-}" ]; then
		mcopy -i "$output@@$partition_offset" "$BOOT98_CFG" ::BOOT98.CFG
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
printf 'Stage 1: %s bytes at LBA 2 (maximum 7168)\n' "$size"
