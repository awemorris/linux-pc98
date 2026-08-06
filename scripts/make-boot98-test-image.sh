#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
output="${1:-$repo/build/boot98/boot98-partition-ipl-test.img}"
heads="${DISK_HEADS:-8}"
sectors="${DISK_SECTORS:-17}"

test ! -e "$output" || {
	echo "Refusing to overwrite existing image: $output" >&2
	exit 1
}
mkdir -p "$(dirname "$output")"
truncate -s "${BOOT98_TEST_MB:-16}M" "$output"

python3 - "$output" "$heads" "$sectors" <<'PY'
import os
import struct
import sys

image, heads, sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
physical_sectors = os.path.getsize(image) // 512
last_lba = physical_sectors - 1

def chs(lba):
    cylinder, rem = divmod(lba, heads * sectors)
    head, sector = divmod(rem, sectors)
    return bytes((sector, head)) + struct.pack("<H", cylinder)

entry = bytearray(32)
entry[0] = 0xA1
entry[1] = 0x91
entry[4:8] = chs(heads * sectors)
entry[8:12] = chs(heads * sectors)
entry[12:16] = chs(last_lba)
entry[16:32] = b"BOOT".ljust(16, b" ")
with open(image, "r+b") as stream:
    stream.seek(512)
    stream.write(entry)
PY

BOOT_LOGO="${BOOT98_LOGO:-}" \
BOOT98_FILES="${BOOT98_FILES:-}" \
DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
	"$repo/scripts/install-boot98-image.sh" --partition 1 \
	--install-disk-stubs "$output" "${BOOT98_KERNEL:-}" "${BOOT98_CFG:-}"

sha256sum "$output"
printf 'BOOT98 IO.SYS test image: %s\n' "$output"
