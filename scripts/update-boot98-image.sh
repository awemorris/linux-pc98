#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader_dir="$repo/bootloader"
image="${1:?usage: $0 IMAGE VMLINUX [BOOT98.CFG]}"
kernel="${2:?usage: $0 IMAGE VMLINUX [BOOT98.CFG]}"
cfg="${3:-}"
heads="${DISK_HEADS:-8}"
sectors="${DISK_SECTORS:-17}"

test -f "$image" || { echo "Image not found: $image" >&2; exit 1; }
test -f "$kernel" || { echo "Kernel not found: $kernel" >&2; exit 1; }
command -v mcopy >/dev/null || { echo "mcopy is required" >&2; exit 1; }

make -C "$bootloader_dir" BOOT98.BIN

# Locate the FAT16 partition named BOOT from the PC-98 partition table.  The
# table stores BIOS logical CHS, so conversion uses the profile geometry.
boot_lba="$(python3 - "$image" "$heads" "$sectors" <<'PY'
import struct
import sys

image, heads, sectors = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(image, "rb") as stream:
    stream.seek(512)
    table = stream.read(512)
for offset in range(0, 512, 32):
    entry = table[offset:offset + 32]
    if not entry or entry[0] == 0:
        continue
    name = entry[16:32].decode("ascii", "replace").rstrip(" \0")
    if name != "BOOT":
        continue
    sector, head = entry[8], entry[9]
    cylinder = struct.unpack_from("<H", entry, 10)[0]
    print((cylinder * heads + head) * sectors + sector)
    break
else:
    raise SystemExit("BOOT partition not found")
PY
)"

offset=$((boot_lba * 512))
mcopy -o -i "$image@@$offset" "$bootloader_dir/BOOT98.BIN" ::BOOT98.BIN
mcopy -o -i "$image@@$offset" "$kernel" ::VMLINUX
if test -n "$cfg"; then
	test -f "$cfg" || { echo "BOOT98.CFG not found: $cfg" >&2; exit 1; }
	mcopy -o -i "$image@@$offset" "$cfg" ::BOOT98.CFG
fi
sync
printf 'Updated BOOT98 image %s (BOOT LBA %s)\n' "$image" "$boot_lba"
