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

printf 'Preparing Remacs bytecode and SKK dictionary...\n'
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
printf 'Checking FAT boot volume at byte offset %s...\n' "$offset"
mdir -i "$image@@$offset" :: >/dev/null
for directory in ::ETC ::BIN ::APPS ::HOME; do
	printf 'Ensuring FAT directory %s...\n' "$directory"
	if ! mdir -i "$image@@$offset" "$directory" >/dev/null 2>&1; then
		mmd -i "$image@@$offset" "$directory"
	fi
done
for obsolete in ::AUTOEXEC.NCT ::APPS/HOLORIS.NAP ::APPS/REMACS.NAP \
    ::HOME/EMACS.EL ::HOME/REMACS.EL; do
	printf 'Removing obsolete overlay path %s if present...\n' "$obsolete"
	mdel -i "$image@@$offset" "$obsolete" 2>/dev/null || true
done
for index in "${!sources[@]}"; do
	printf 'Installing overlay file %s -> %s (%s bytes)...\n' \
		"${sources[$index]}" "${destinations[$index]}" \
		"$(stat -c %s "${sources[$index]}")"
	mcopy -o -i "$image@@$offset" "${sources[$index]}" "${destinations[$index]}"
done
# mtools closes the image when each command exits but does not promise that
# dirty pages have reached storage.  Flush this image only; never sync the
# host filesystem or unrelated builds.
printf 'Flushing bootloader overlay writes for %s...\n' "$image"
python3 - "$image" <<'PY'
import os
import sys

descriptor = os.open(sys.argv[1], os.O_RDWR)
try:
    os.fdatasync(descriptor)
finally:
    os.close(descriptor)
PY
printf 'Verifying installed bootloader overlay...\n'
"$repo/bootloader/verify-fs.sh" "$image" "$offset"
