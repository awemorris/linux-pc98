#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
profile=""
partition=1
heads=8
sectors=17
cmdline=""

usage()
{
	cat <<'EOF'
Usage: bootsimple/install-image.sh --profile NAME --cmdline STRING
       [--partition N] [--heads H] [--sectors S] IMAGE VMLINUX

Recreate only the selected FAT16 BOOT partition and install the assembly-only
IO.SYS direct Linux loader. Root and Linux swap partitions are not modified.
EOF
}

while test "$#" -gt 0; do
	case "$1" in
		--profile | --partition | --heads | --sectors | --cmdline)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--profile) profile="$2" ;;
				--partition) partition="$2" ;;
				--heads) heads="$2" ;;
				--sectors) sectors="$2" ;;
				--cmdline) cmdline="$2" ;;
			esac
			shift 2
			;;
		-h | --help) usage; exit 0 ;;
		*) break ;;
	esac
done

test "$#" -eq 2 || { usage >&2; exit 2; }
image="$1"
kernel="$2"
test -n "$profile" || { echo "--profile is required" >&2; exit 2; }
test -n "$cmdline" || { echo "--cmdline is required" >&2; exit 2; }
case "$partition:$heads:$sectors" in
	*[!0-9:]*) echo "partition, heads and sectors must be integers" >&2; exit 2 ;;
esac
test "$partition" -ge 1 && test "$partition" -le 16 || {
	echo "partition must be in 1..16" >&2; exit 2
}
test "$heads" -gt 0 && test "$sectors" -gt 0 || {
	echo "heads and sectors must be positive" >&2; exit 2
}
test -f "$image" || { echo "Image not found: $image" >&2; exit 1; }
test -s "$kernel" || { echo "VMLINUX not found: $kernel" >&2; exit 1; }
for command in dd mattrib mcopy mformat python3 stat; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

"$repo/bootsimple/verify-image.py" elf "$kernel"
build="$repo/build/bootsimple/$profile"
"$repo/bootsimple/build.sh" --profile "$profile" \
	--output-dir "$build" --cmdline "$cmdline"

layout="$(python3 - "$image" "$heads" "$sectors" "$partition" <<'PY'
import os
import struct
import sys

image, heads, sectors, selected = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])

def lba(raw):
    sector, head = raw[0], raw[1]
    cylinder = struct.unpack_from("<H", raw, 2)[0]
    if sector >= sectors or head >= heads:
        raise SystemExit("partition CHS does not match requested geometry")
    return (cylinder * heads + head) * sectors + sector

def chs(cylinder, head=0, sector=0):
    return bytes((sector, head)) + struct.pack("<H", cylinder)

with open(image, "r+b") as stream:
    stream.seek(512)
    table = bytearray(stream.read(512))
    if len(table) != 512:
        raise SystemExit("short PC-98 partition table")
    offset = (selected - 1) * 32
    entry = table[offset:offset + 32]
    if entry[0] == 0:
        raise SystemExit("selected partition is empty")
    start = lba(entry[8:12])
    end = lba(entry[12:16])
    if start > end or (end + 1) * 512 > os.path.getsize(image):
        raise SystemExit("selected partition lies outside image")
    cylinder, remainder = divmod(start, heads * sectors)
    if remainder != 0:
        raise SystemExit("BOOT partition must begin on a cylinder boundary")
    table[offset] = (table[offset] & 0x7f) | 0x80
    table[offset + 1] = 0x91
    table[offset + 4:offset + 8] = chs(cylinder)
    table[offset + 8:offset + 12] = chs(cylinder)
    table[offset + 16:offset + 32] = b"BOOT".ljust(16, b" ")
    stream.seek(512)
    stream.write(table)
    print(start, end - start + 1)
PY
)"
read -r boot_lba boot_sectors <<<"$layout"
test $((boot_sectors % 2)) -eq 0 || {
	echo "BOOT partition must contain an even number of physical sectors" >&2
	exit 1
}
offset=$((boot_lba * 512))
logical_sectors=$((boot_sectors / 2))
cluster_sectors=1
while test $((logical_sectors / cluster_sectors)) -ge 65525; do
	cluster_sectors=$((cluster_sectors * 2))
done
test $((logical_sectors / cluster_sectors)) -ge 4085 || {
	echo "BOOT partition is too small for FAT16" >&2
	exit 1
}

dd if="$build/ipl-lba0.bin" of="$image" bs=512 count=1 \
	conv=notrunc status=none
dd if="$build/ipl-lba2.bin" of="$image" bs=512 seek=2 count=14 \
	conv=notrunc status=none
mformat -i "$image@@$offset" -S 3 -c "$cluster_sectors" -h "$heads" \
	-s "$sectors" -H "$boot_lba" -T "$logical_sectors" -v BOOT ::
python3 - "$image" "$offset" "$boot_lba" "$build/partition-pbr.bin" <<'PY'
import struct
import sys

image, offset, partition_lba, pbr_path = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
with open(image, "r+b") as stream:
    stream.seek(offset)
    bpb = stream.read(1024)
    pbr = bytearray(open(pbr_path, "rb").read())
    if len(bpb) != 1024 or len(pbr) != 1024:
        raise SystemExit("short BPB or partition PBR")
    pbr[3:0x3e] = bpb[3:0x3e]
    struct.pack_into("<I", pbr, 0x1c, partition_lba)
    struct.pack_into("<H", pbr, 0x0e, 1)
    pbr[0x1fe:0x200] = b"\x55\xaa"
    pbr[0x3fe:0x400] = b"\x55\xaa"
    stream.seek(offset)
    stream.write(pbr)
PY

# PBR does not walk the IO.SYS FAT chain, so copy it first into the empty FAT.
mcopy -o -i "$image@@$offset" "$build/IO.SYS" ::IO.SYS
mattrib -i "$image@@$offset" +r +h +s ::IO.SYS
mcopy -o -i "$image@@$offset" "$kernel" ::VMLINUX
mattrib -i "$image@@$offset" +r +h +s ::VMLINUX

"$repo/bootsimple/verify-image.py" all "$image" \
	--heads "$heads" --sectors "$sectors" --partition "$partition" \
	--kernel "$kernel" --loader-dir "$build"
