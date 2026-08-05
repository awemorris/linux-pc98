#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader="$repo/bootloader"
image="${1:?usage: $0 IMAGE [VMLINUX [BOOT98.CFG]]}"
kernel="${2:-}"
cfg="${3:-}"
heads="${DISK_HEADS:-8}"
sectors="${DISK_SECTORS:-17}"

test -f "$image" || { echo "Image not found: $image" >&2; exit 1; }
test -z "$kernel" || test -f "$kernel" || {
	echo "Kernel not found: $kernel" >&2
	exit 1
}
case "$heads:$sectors" in
	*[!0-9:]* | 0:* | *:0) echo "Invalid geometry: H=$heads S=$sectors" >&2; exit 2 ;;
esac
for command in dd mcopy python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

make -C "$bootloader" disk-ipl.bin boot98-stage1.bin BOOT98.BIN
stage1_size="$(stat -c %s "$bootloader/boot98-stage1.bin")"
test "$stage1_size" -le 7168 || {
	echo "BOOT98 Stage 1 exceeds its 14-sector area: $stage1_size" >&2
	exit 1
}

# BOOT98 Stage 2 occupies the 14 sectors beginning at LBA 2. Clear that
# complete region before replacing a legacy FAT loader. The generic LBA 0 IPL
# reads only LBA 2; its bootstrap owns the remaining-sector format.
dd if=/dev/zero of="$image" bs=512 seek=2 count=14 conv=notrunc status=none
dd if="$bootloader/disk-ipl.bin" of="$image" bs=512 count=1 \
	conv=notrunc status=none
dd if="$bootloader/boot98-stage1.bin" of="$image" bs=512 seek=2 \
	conv=notrunc status=none

# Rename the first FAT16 partition to BOOT without changing any geometry.
# Stage 1 discovers BOOT98.BIN through this native PC-98 partition entry.
boot_lba="$(python3 - "$image" "$heads" "$sectors" <<'PY'
import struct
import sys

image, heads, sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(image, "r+b") as stream:
    stream.seek(512)
    table = bytearray(stream.read(512))
    for offset in range(0, 512, 32):
        entry = table[offset:offset + 32]
        if entry[0] == 0:
            continue
        if entry[1] not in (0x20, 0x81):
            continue
        sector, head = entry[8], entry[9]
        cylinder = struct.unpack_from("<H", entry, 10)[0]
        lba = (cylinder * heads + head) * sectors + sector
        table[offset + 16:offset + 32] = b"BOOT".ljust(16, b" ")
        stream.seek(512)
        stream.write(table)
        print(lba)
        break
    else:
        raise SystemExit("FAT16 boot partition not found")
PY
)"

offset=$((boot_lba * 512))
mcopy -o -i "$image@@$offset" "$bootloader/BOOT98.BIN" ::BOOT98.BIN
if test -n "$kernel"; then
	mcopy -o -i "$image@@$offset" "$kernel" ::VMLINUX
fi
if test -n "$cfg"; then
	test -f "$cfg" || { echo "BOOT98.CFG not found: $cfg" >&2; exit 1; }
	mcopy -o -i "$image@@$offset" "$cfg" ::BOOT98.CFG
fi
sync
printf 'Installed BOOT98 in %s (BOOT LBA %s, Stage 1 %s bytes)\n' \
	"$image" "$boot_lba" "$stage1_size"
