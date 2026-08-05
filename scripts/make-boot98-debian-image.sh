#!/usr/bin/env bash
set -euo pipefail

PATH="/usr/sbin:/usr/bin:/sbin:/bin:$PATH"

repo="$(cd "$(dirname "$0")/.." && pwd)"
bootloader_dir="$repo/bootloader"
rootfs="${1:?usage: $0 ROOTFS OUTPUT [VMLINUX]}"
output="${2:?usage: $0 ROOTFS OUTPUT [VMLINUX]}"
kernel="${3:-$repo/build/kernel-7.1-i486/vmlinux.boot}"

heads="${DISK_HEADS:-8}"
sectors="${DISK_SECTORS:-17}"
swap_mb="${SWAP_MB:-128}"
root_password="${ROOT_PASSWORD:-pc98}"

test -d "$rootfs" || {
	echo "Root filesystem not found: $rootfs" >&2
	exit 1
}
test -f "$kernel" || {
	echo "Kernel not found: $kernel" >&2
	echo "Build it with: ./build.sh kernel --cpu 486" >&2
	exit 1
}
if test -e "$output"; then
	echo "Refusing to overwrite existing image: $output" >&2
	exit 1
fi

for command in mformat mcopy mkfs.ext4 mkswap mountpoint python3 sudo; do
	command -v "$command" >/dev/null || {
		echo "$command is required" >&2
		exit 1
	}
done

case "$heads:$sectors:$swap_mb" in
	*[!0-9:]* | 0:* | *:0:* | *:*:0)
		echo "Heads, sectors, and swap size must be positive integers" >&2
		exit 1
		;;
esac

# BOOT98 uses BIOS logical CHS for the PC-98 partition table.  The first
# cylinder is the NEC system area.  The FAT16 BOOT volume ends at cylinder
# 2048 and the ext4 root volume ends at cylinder 15419.  Swap is appended so
# adding it cannot change the offsets of an existing root filesystem layout.
cylinder_sectors=$((heads * sectors))
cylinder_bytes=$((cylinder_sectors * 512))
ipl_start=$((1 * cylinder_sectors))
boot_start=$((2 * cylinder_sectors))
root_start=$((2049 * cylinder_sectors))
root_last_cylinder=15419
swap_start_cylinder=$((root_last_cylinder + 1))
swap_cylinders=$(((swap_mb * 1024 * 1024 + cylinder_bytes - 1) /
	cylinder_bytes))
swap_last_cylinder=$((swap_start_cylinder + swap_cylinders - 1))
total_cylinders=$((swap_last_cylinder + 1))

boot_sectors=$((root_start - boot_start))
root_sectors=$(((root_last_cylinder - 2049 + 1) * cylinder_sectors))
swap_start=$((swap_start_cylinder * cylinder_sectors))
swap_sectors=$((swap_cylinders * cylinder_sectors))
total_bytes=$((total_cylinders * cylinder_bytes))
root_bytes=$((root_sectors * 512))
swap_bytes=$((swap_sectors * 512))

work="$(mktemp -d "${TMPDIR:-/tmp}/boot98-debian.XXXXXX")"
root_image="$work/root.ext4"
swap_image="$work/swap.img"
mount_dir="$work/root"
cfg="$work/BOOT98.CFG"
mounted=0

cleanup()
{
	if test "$mounted" -eq 1 && mountpoint -q "$mount_dir"; then
		sudo umount "$mount_dir"
	fi
	rm -f -- "$root_image" "$swap_image" "$cfg"
	rmdir "$mount_dir" "$work" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

make -C "$bootloader_dir" disk-ipl.bin boot98-stage1.bin \
	boot98-chain-test.bin BOOT98.BIN
mkdir -p "$(dirname "$output")" "$mount_dir"
truncate -s "$total_bytes" "$output"
dd if="$bootloader_dir/disk-ipl.bin" of="$output" bs=512 count=1 \
	conv=notrunc status=none
dd if="$bootloader_dir/boot98-stage1.bin" of="$output" bs=512 seek=2 \
	conv=notrunc status=none
dd if="$bootloader_dir/boot98-chain-test.bin" of="$output" bs=512 \
	seek="$ipl_start" count=1 conv=notrunc status=none
python3 - "$output" "$root_last_cylinder" "$swap_start_cylinder" \
	"$swap_last_cylinder" "$heads" "$sectors" <<'PY'
import struct
import sys

image = sys.argv[1]
root_last = int(sys.argv[2])
swap_start = int(sys.argv[3])
swap_last = int(sys.argv[4])
last_head = int(sys.argv[5]) - 1
last_sector = int(sys.argv[6]) - 1


def chs(cylinder, head=0, sector=0):
    return bytes((sector, head)) + struct.pack("<H", cylinder)


def entry(flags, kind, first_ipl, first_data, last, name):
    value = bytearray(32)
    value[0] = flags
    value[1] = kind
    value[4:8] = chs(first_ipl)
    value[8:12] = chs(first_data)
    value[12:16] = chs(last, last_head, last_sector)
    value[16:32] = name.encode("ascii").ljust(16, b" ")
    return value


table = bytearray(512)
table[0:32] = entry(0xA1, 0x20, 1, 2, 2048, "BOOT")
table[32:64] = entry(0x21, 0x83, 2049, 2049, root_last, "DEBIAN13")
table[64:96] = entry(0x21, 0x82, swap_start, swap_start, swap_last,
                     "LINUXSWAP")
with open(image, "r+b") as stream:
    stream.seek(512)
    stream.write(table)
PY

mformat -i "$output@@$((boot_start * 512))" -c 8 -h "$heads" \
	-s "$sectors" -T "$boot_sectors" -v BOOT ::
printf '%s\n' \
	'echo Booting Debian 13 i486...' \
	'kernel VMLINUX' \
	'arg root=/dev/sda2 rootfstype=ext4 rw' \
	'boot' >"$cfg"
mcopy -i "$output@@$((boot_start * 512))" \
	"$bootloader_dir/BOOT98.BIN" ::BOOT98.BIN
mcopy -i "$output@@$((boot_start * 512))" "$kernel" ::VMLINUX
mcopy -i "$output@@$((boot_start * 512))" "$cfg" ::BOOT98.CFG

truncate -s "$root_bytes" "$root_image"
mkfs.ext4 -q -F -L DEBIAN13 "$root_image"
sudo mount -o loop "$root_image" "$mount_dir"
mounted=1
sudo cp -a "$rootfs"/. "$mount_dir"/

# The kernel mounts devtmpfs.  Keep udev installed for later manual use, but
# avoid its full early-device trigger on this memory-constrained live image.
sudo rm -f "$mount_dir/etc/rcS.d/S02udev"
printf '%s\n' \
	'/dev/sda2 / ext4 defaults,noatime 0 1' \
	'/dev/sda3 none swap sw 0 0' \
	'proc /proc proc defaults 0 0' \
	'sysfs /sys sysfs defaults 0 0' | \
	sudo tee "$mount_dir/etc/fstab" >/dev/null
printf '%s\n' 'debian-pc98' | \
	sudo tee "$mount_dir/etc/hostname" >/dev/null
printf '%s\n' \
	'127.0.0.1 localhost' \
	'127.0.1.1 debian-pc98' | \
	sudo tee "$mount_dir/etc/hosts" >/dev/null
printf '%s\n' \
	'auto lo' \
	'iface lo inet loopback' | \
	sudo tee "$mount_dir/etc/network/interfaces" >/dev/null
printf '%s\n' 'CONFIGURE_INTERFACES=no' | \
	sudo tee "$mount_dir/etc/default/networking" >/dev/null
printf '%s\n' \
	'deb [trusted=yes] https://noctvm.io/debian-i486/packages trixie main pc98' | \
	sudo tee "$mount_dir/etc/apt/sources.list" >/dev/null
printf '%s\n' \
	'Debian GNU/Linux 13 (trixie) i486 PC-98 BOOT98 image' \
	"Login: root  Password: $root_password" | \
	sudo tee "$mount_dir/etc/motd" >/dev/null
printf 'root:%s\n' "$root_password" | sudo chroot "$mount_dir" chpasswd
sudo chroot "$mount_dir" apt-get clean
sudo sync
sudo umount "$mount_dir"
mounted=0

dd if="$root_image" of="$output" bs=512 seek="$root_start" \
	conv=notrunc,sparse status=progress
truncate -s "$swap_bytes" "$swap_image"
mkswap --quiet --label PC98SWAP "$swap_image"
dd if="$swap_image" of="$output" bs=512 seek="$swap_start" \
	conv=notrunc,sparse status=progress
sync

sha256sum "$output"
printf 'BOOT98 Debian 13 i486 image: %s\n' "$output"
printf 'Geometry: C=%d H=%d S=%d, root=/dev/sda2, swap=/dev/sda3 (%d MiB)\n' \
	"$total_cylinders" "$heads" "$sectors" "$swap_mb"
printf 'Login: root / %s\n' "$root_password"
