#!/usr/bin/env bash
set -euo pipefail

# End-to-end test for the safe M11 Noct utilities.  Only a private image below
# build/tests is modified; the release image is copied before BOOT.SYS or FAT
# contents are changed.
repo="$(cd "$(dirname "$0")/.." && pwd)"
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
bios_dir="${PC98_BIOS_DIR:-$repo/qemu-pc98/roms/pc98bios}"
base="$repo/build/releases/linux-pc98-i386sx-busybox-ide.img"
work="$repo/build/tests/boot98-m11-utilities"
image="$work/m11-ide.raw"
files="$work/files"
cfg="$work/BOOT.CFG"
source_file="$files/SOURCE.BIN"
copied_file="$work/COPY.BIN"

test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || {
	echo "PC-98 BIOS directory not found: $bios_dir" >&2
	exit 1
}
test -f "$base" || { echo "M11 source image not found: $base" >&2; exit 1; }
for command in cmp mcopy python3 timeout; do
	command -v "$command" >/dev/null || {
		echo "$command is required" >&2
		exit 1
	}
done

mkdir -p "$work" "$files"
cp --reflink=auto "$base" "$image"
install -m 0644 "$repo/bootloader/LS.NCT" "$files/LS.NCT"
install -m 0644 "$repo/bootloader/CP.NCT" "$files/CP.NCT"
python3 - "$source_file" <<'PY'
import sys

with open(sys.argv[1], "wb") as stream:
    stream.write(bytes((index * 37 + 11) & 0xff for index in range(16417)))
PY
printf 'ls\ncp SOURCE.BIN COPY.BIN\nhalt\n' > "$cfg"
make -C "$repo/bootloader" -j"$(nproc)" BOOT.SYS
BOOT98_FILES="$files" DISK_HEADS=8 DISK_SECTORS=17 \
	"$repo/scripts/install-boot98-image.sh" "$image" "" "$cfg"

offset="$(python3 - "$image" <<'PY'
import struct
import sys

with open(sys.argv[1], "rb") as stream:
    stream.seek(512)
    table = stream.read(512)
for offset in range(0, 512, 32):
    entry = table[offset:offset + 32]
    if entry[0] and entry[16:32] == b"BOOT".ljust(16, b" "):
        cylinder = struct.unpack_from("<H", entry, 6)[0]
        print(cylinder * 8 * 17 * 512)
        break
else:
    raise SystemExit("BOOT partition not found")
PY
)"

set +e
timeout --signal=INT --kill-after=5 45 \
	"$qemu" -M pc9801 -cpu 386 -m 6 -accel tcg -L "$bios_dir" \
	-nic none -drive "if=ide,bus=0,unit=0,format=raw,file=$image" \
	-display none -serial none -monitor none -no-reboot >/dev/null 2>&1
status=$?
set -e
if test "$status" -ne 0 && test "$status" -ne 124; then
	echo "M11 QEMU failed with status $status" >&2
	exit 1
fi
rm -f -- "$copied_file"
mcopy -i "$image@@$offset" ::COPY.BIN "$copied_file"
cmp -s "$source_file" "$copied_file" || {
	echo "M11 CP.NCT result differs from SOURCE.BIN" >&2
	exit 1
}
printf 'M11 LS.NCT/CP.NCT QEMU test: PASS (%s)\n' "$image"
