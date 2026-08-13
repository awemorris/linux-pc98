#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
test "$#" -eq 2 || { echo "usage: $0 IMAGE BYTE_OFFSET" >&2; exit 2; }
image="$1"
offset="$2"
base="$repo/bootloader/fs"
sources=(
	"$base/etc/zinit.rc"
	"$base/bin/menu.nct"
	"$base/bin/menuback.bmp"
	"$base/apps/holoris.nct"
	"$base/apps/emacs.nap"
	"$base/home/skkjisyo.dic"
)
destinations=(
	::ETC/ZINIT.RC
	::BIN/MENU.NCT
	::BIN/MENUBACK.BMP
	::APPS/HOLORIS.NCT
	::APPS/EMACS.NAP
	::HOME/SKKJISYO.DIC
)
for command in cmp dd mdir mtype python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done
for index in "${!sources[@]}"; do
	printf 'Verifying overlay file %s...\n' "${destinations[$index]}"
	mdir -i "$image@@$offset" "${destinations[$index]}" >/dev/null
	mtype -i "$image@@$offset" "${destinations[$index]}" | \
		cmp -s -- "${sources[$index]}" - || {
		echo "FAT overlay mismatch: ${destinations[$index]}" >&2
		exit 1
	}
done

printf 'Verifying bootloader overlay configuration and assets...\n'
test "$(wc -l < "$base/etc/zinit.rc")" -eq 1
grep -qx '/bin/noct /bin/menu.nct' "$base/etc/zinit.rc"
grep -q '/bin/menuback.bmp' "$base/bin/menu.nct"
grep -q '/bin/linux /vmlinux' "$base/bin/menu.nct"
grep -q '/bin/noct /apps/holoris.nct' "$base/bin/menu.nct"
if grep -qi 'boot\.cfg' "$base/bin/menu.nct"; then
	echo "menu Linux action must not source boot.cfg" >&2
	exit 1
fi
test "$(dd if="$base/apps/emacs.nap" bs=1 count=13 status=none)" = \
	"Noct Bytecode"
python3 - "$base/bin/menuback.bmp" <<'PY'
import struct
import sys
with open(sys.argv[1], "rb") as stream:
    header = stream.read(54)
if len(header) < 54 or header[:2] != b"BM":
    raise SystemExit("menuback.bmp has no BMP header")
width, height = struct.unpack_from("<ii", header, 18)
bpp = struct.unpack_from("<H", header, 28)[0]
if (width, abs(height), bpp) != (640, 480, 8):
    raise SystemExit(f"menuback.bmp must be 640x480x8, got {width}x{height}x{bpp}")
PY
for obsolete in ::AUTOEXEC.NCT ::APPS/HOLORIS.NAP ::APPS/REMACS.NAP \
    ::HOME/EMACS.EL ::HOME/REMACS.EL; do
	if mdir -i "$image@@$offset" "$obsolete" >/dev/null 2>&1; then
		echo "obsolete overlay file exists: $obsolete" >&2
		exit 1
	fi
done
echo "linux-pc98 bootloader overlay verified at byte offset $offset"
