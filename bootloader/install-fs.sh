#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
partition=1
offset=""
heads="${DISK_HEADS:-8}"
sectors="${DISK_SECTORS:-17}"

usage()
{
	echo "usage: $0 [--partition N | --offset-bytes N | --offset-sectors N] IMAGE" >&2
}

while test "$#" -gt 0; do
	case "$1" in
		--partition) test "$#" -ge 2 || { usage; exit 2; }; partition="$2"; shift 2 ;;
		--offset-bytes) test "$#" -ge 2 || { usage; exit 2; }; offset="$2"; shift 2 ;;
		--offset-sectors) test "$#" -ge 2 || { usage; exit 2; }; offset=$(("$2" * 512)); shift 2 ;;
		-h|--help) usage; exit 0 ;;
		--) shift; break ;;
		-*) usage; exit 2 ;;
		*) break ;;
	esac
done
test "$#" -eq 1 || { usage; exit 2; }
image="$1"
test -f "$image" || { echo "Image not found: $image" >&2; exit 1; }
for command in mcopy mdel mdir mmd python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

case "$heads:$sectors:$partition" in
	*[!0-9:]*) echo "Invalid geometry or partition" >&2; exit 2 ;;
esac
if test -z "$offset"; then
	if test "$partition" -eq 0; then
		offset=0
	else
		offset="$(python3 - "$image" "$partition" "$heads" "$sectors" <<'PY'
import struct
import sys

image, partition, heads, sectors = sys.argv[1], *map(int, sys.argv[2:])
if not 1 <= partition <= 16:
    raise SystemExit("partition must be 1..16")
with open(image, "rb") as stream:
    stream.seek(512 + (partition - 1) * 32)
    entry = stream.read(32)
if len(entry) != 32 or entry[0] == 0:
    raise SystemExit(f"PC-98 partition {partition} is absent")
sector, head = entry[8], entry[9]
cylinder = struct.unpack_from("<H", entry, 10)[0]
print(((cylinder * heads + head) * sectors + sector) * 512)
PY
)"
	fi
fi
case "$offset" in
	''|*[!0-9]*) echo "Invalid byte offset: $offset" >&2; exit 2 ;;
esac
test "$offset" -lt "$(stat -c %s "$image")" || {
	echo "BOOT partition offset is outside the image" >&2
	exit 1
}

"$repo/bootloader/build-remacs.sh"
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
for source in "${sources[@]}"; do
	test -s "$source" || { echo "Overlay source is missing: $source" >&2; exit 1; }
done
mdir -i "$image@@$offset" :: >/dev/null
for directory in ::ETC ::BIN ::APPS ::HOME; do
	mmd -i "$image@@$offset" "$directory" 2>/dev/null || true
done
for obsolete in ::AUTOEXEC.NCT ::APPS/HOLORIS.NAP ::APPS/REMACS.NAP \
    ::HOME/EMACS.EL ::HOME/REMACS.EL; do
	mdel -i "$image@@$offset" "$obsolete" 2>/dev/null || true
done
for index in "${!sources[@]}"; do
	mcopy -o -i "$image@@$offset" "${sources[$index]}" "${destinations[$index]}"
done
"$repo/bootloader/verify-fs.sh" "$image" "$offset"
