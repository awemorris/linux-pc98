#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader="$repo/bootloader"
partition="${BOOT_PARTITION:-0}"
install_disk_stubs="${INSTALL_DISK_STUBS:-0}"
while test "$#" -gt 0; do
	case "$1" in
		--partition)
			test "$#" -ge 2 || { echo "Missing value for --partition" >&2; exit 2; }
			partition="$2"
			shift 2
			;;
		--install-disk-stubs)
			install_disk_stubs=1
			shift
			;;
		*) break ;;
	esac
done
image="${1:?usage: $0 [--partition N] [--install-disk-stubs] IMAGE [VMLINUX [BOOT.CFG]]}"
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
case "$partition" in
	'' | *[!0-9]* | 0) test "$partition" = 0 || { echo "Invalid partition: $partition" >&2; exit 2; } ;;
	*) test "$partition" -le 16 || { echo "Invalid partition: $partition" >&2; exit 2; } ;;
esac
case "$install_disk_stubs" in
	0 | 1) ;;
	*) echo "INSTALL_DISK_STUBS must be 0 or 1" >&2; exit 2 ;;
esac
for command in dd mattrib mcopy mformat python3; do
	command -v "$command" >/dev/null || { echo "$command is required" >&2; exit 1; }
done

make -C "$bootloader" ipl-lba0.bin ipl-lba2.bin \
	ipl-lba0.img ipl-lba2.img ipl-part.img IO.SYS boot.bin
io_sys_size="$(stat -c %s "$bootloader/IO.SYS")"

# Recreate the first FAT16 partition as the BOOT environment.  The volume has
# one 1024-byte reserved logical sector containing the PBR.  IO.SYS is copied
# first as a normal contiguous FAT file, matching the historical DOS SYS-file
# layout.  Root and swap partitions remain untouched.
layout="$(python3 - "$image" "$heads" "$sectors" "$partition" <<'PY'
import struct
import sys

image, heads, sectors, selected = (
    sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))

def lba(raw):
    sector, head = raw[0], raw[1]
    cylinder = struct.unpack_from("<H", raw, 2)[0]
    return (cylinder * heads + head) * sectors + sector

def chs(cylinder, head=0, sector=0):
    return bytes((sector, head)) + struct.pack("<H", cylinder)

with open(image, "r+b") as stream:
    stream.seek(512)
    table = bytearray(stream.read(512))
    for offset in range(0, 512, 32):
        index = offset // 32 + 1
        if selected and index != selected:
            continue
        entry = table[offset:offset + 32]
        if entry[0] == 0:
            continue
        if not selected:
            name = bytes(entry[16:32])
            sid_type = entry[1] & 0x7f
            if name != b"BOOT".ljust(16, b" ") and sid_type not in (
                    0x01, 0x11, 0x20):
                continue
        ipl_cylinder = struct.unpack_from("<H", entry, 6)[0]
        # Keep the IPL and FAT data start identical so the partition PBR is
        # also DOS logical sector zero.
        ipl_lba = ipl_cylinder * heads * sectors
        data_cylinder = ipl_cylinder
        data_lba = ipl_lba
        end_lba = lba(entry[12:16])
        if data_lba > end_lba:
            raise SystemExit("BOOT partition is empty")
        table[offset] |= 0x80
        # MID bit 7 marks the partition active.  SID bit 7 marks it
        # bootable; SID type 0x11 identifies a PC-98 DOS FAT16 volume.
        # Thus A1/91 is visible as a DOS drive and selectable by the NEC
        # fixed-disk boot menu.
        table[offset + 1] = 0x91
        table[offset + 4:offset + 8] = chs(ipl_cylinder)
        table[offset + 8:offset + 12] = chs(data_cylinder)
        table[offset + 16:offset + 32] = b"BOOT".ljust(16, b" ")
        stream.seek(512)
        stream.write(table)
        print(ipl_lba, data_lba, end_lba - data_lba + 1, index)
        break
    else:
        raise SystemExit("FAT16 boot partition not found")
PY
)"
read -r ipl_lba boot_lba boot_sectors partition <<<"$layout"
offset=$((boot_lba * 512))
test $((boot_sectors % 2)) -eq 0 || {
	echo "BOOT partition must contain an even number of physical sectors" >&2
	exit 1
}
logical_sectors=$((boot_sectors / 2))

if test "$install_disk_stubs" -eq 1; then
	dd if="$bootloader/ipl-lba0.bin" of="$image" bs=512 count=1 \
		conv=notrunc status=none
	dd if="$bootloader/ipl-lba2.bin" of="$image" bs=512 seek=2 count=14 \
		conv=notrunc status=none
fi
cluster_sectors=1
while test $((logical_sectors / cluster_sectors)) -ge 65525; do
	cluster_sectors=$((cluster_sectors * 2))
done
test $((logical_sectors / cluster_sectors)) -ge 4085 || {
	echo "BOOT partition is too small for FAT16" >&2
	exit 1
}
mformat -i "$image@@$offset" -S 3 -c "$cluster_sectors" -h "$heads" \
	-s "$sectors" -H "$boot_lba" -T "$logical_sectors" -v BOOT ::
python3 - "$image" "$offset" "$boot_lba" \
	"$bootloader/partition-pbr.bin" <<'PY'
import struct
import sys

image = sys.argv[1]
offset = int(sys.argv[2])
partition_lba = int(sys.argv[3])
pbr_path = sys.argv[4]

with open(image, "r+b") as stream:
	stream.seek(offset)
	bpb = stream.read(1024)
	if len(bpb) != 1024:
		raise SystemExit("short FAT16 reserved sector")
	with open(pbr_path, "rb") as source:
		pbr = bytearray(source.read())
	if len(pbr) != 1024:
		raise SystemExit("partition-pbr.bin is not 1024 bytes")
	pbr[3:0x3e] = bpb[3:0x3e]
	struct.pack_into("<I", pbr, 0x1c, partition_lba)
	struct.pack_into("<H", pbr, 0x0e, 1)
	pbr[0x1fe:0x200] = b"\x55\xaa"
	pbr[0x3fe:0x400] = b"\x55\xaa"
	stream.seek(offset)
	stream.write(pbr)
PY
mcopy -o -i "$image@@$offset" "$bootloader/IO.SYS" ::IO.SYS
mattrib -i "$image@@$offset" +r +h +s ::IO.SYS
mcopy -o -i "$image@@$offset" "$bootloader/boot.bin" ::BOOT.BIN
if test -n "$kernel"; then
	mcopy -o -i "$image@@$offset" "$kernel" ::VMLINUX
fi
if test -n "$cfg"; then
	test -f "$cfg" || { echo "BOOT.CFG not found: $cfg" >&2; exit 1; }
	mcopy -o -i "$image@@$offset" "$cfg" ::BOOT.CFG
fi
if test -f "$bootloader/dos/linux98.exe"; then
	mcopy -o -i "$image@@$offset" "$bootloader/dos/linux98.exe" ::LINUX98.EXE
fi
if test -f "$bootloader/dos/inst.exe"; then
	mcopy -o -i "$image@@$offset" "$bootloader/dos/inst.exe" ::INST.EXE
	mcopy -o -i "$image@@$offset" "$bootloader/ipl-lba0.img" ::IPL-LBA0.IMG
	mcopy -o -i "$image@@$offset" "$bootloader/ipl-lba2.img" ::IPL-LBA2.IMG
	mcopy -o -i "$image@@$offset" "$bootloader/ipl-part.img" ::IPL-PART.IMG
fi
if test -n "${BOOT_LOGO:-}"; then
	test -f "$BOOT_LOGO" || { echo "Boot logo not found: $BOOT_LOGO" >&2; exit 1; }
	mcopy -o -i "$image@@$offset" "$BOOT_LOGO" ::LOGO.RAW
fi
if test -n "${BOOT98_FILES:-}"; then
	for file in "$BOOT98_FILES"/*; do
		test -f "$file" || continue
		mcopy -o -i "$image@@$offset" "$file" ::
	done
fi
sync
printf 'Installed BOOT98 in %s partition %s (PBR LBA %s, IO.SYS %s bytes)\n' \
	"$image" "$partition" "$ipl_lba" "$io_sys_size"
if test "$install_disk_stubs" -eq 1; then
	printf 'Installed distributed disk stubs at LBA 0 and LBA 2-15\n'
else
	printf 'Preserved existing disk IPL code at LBA 0 and LBA 2-15\n'
fi
