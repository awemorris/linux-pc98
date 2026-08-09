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

# BOOT98 uses BIOS logical CHS for the PC-98 partition table.  Cylinder zero
# is the NEC system area and the FAT16 BOOT volume begins at cylinder one.
# The FAT16 PBR occupies one 1024-byte reserved sector and IO.SYS is the first
# ordinary FAT file, so DOS and the partition IPL use the same start CHS.
# Keep the original H=8/S=17 image's BOOT and root byte
# capacities,
# then round each one up to whole cylinders for the selected geometry.  Fixed
# ending cylinder numbers would make an H=8/S=32 SCSI image about 1.88 times
# larger than the IDE image even though both contain the same files.
cylinder_sectors=$((heads * sectors))
cylinder_bytes=$((cylinder_sectors * 512))
baseline_cylinder_bytes=$((8 * 17 * 512))
boot_target_bytes=$((2047 * baseline_cylinder_bytes))
root_target_bytes=$((13371 * baseline_cylinder_bytes))

boot_start_cylinder=1
boot_cylinders=$(((boot_target_bytes + cylinder_bytes - 1) / cylinder_bytes))
boot_last_cylinder=$((boot_start_cylinder + boot_cylinders - 1))
root_start_cylinder=$((boot_last_cylinder + 1))
root_cylinders=$(((root_target_bytes + cylinder_bytes - 1) / cylinder_bytes))
root_last_cylinder=$((root_start_cylinder + root_cylinders - 1))
swap_start_cylinder=$((root_last_cylinder + 1))
swap_cylinders=$(((swap_mb * 1024 * 1024 + cylinder_bytes - 1) /
	cylinder_bytes))
swap_last_cylinder=$((swap_start_cylinder + swap_cylinders - 1))
total_cylinders=$((swap_last_cylinder + 1))

root_start=$((root_start_cylinder * cylinder_sectors))
boot_sectors=$((boot_cylinders * cylinder_sectors))
root_sectors=$((root_cylinders * cylinder_sectors))
swap_start=$((swap_start_cylinder * cylinder_sectors))
swap_sectors=$((swap_cylinders * cylinder_sectors))
total_bytes=$((total_cylinders * cylinder_bytes))
root_bytes=$((root_sectors * 512))
swap_bytes=$((swap_sectors * 512))

work="$(mktemp -d "${TMPDIR:-/tmp}/boot98-debian.XXXXXX")"
root_image="$work/root.ext4"
swap_image="$work/swap.img"
mount_dir="$work/root"
cfg="$work/BOOT.CFG"
kernel_extra_args="${KERNEL_EXTRA_ARGS:-}"
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

make -C "$bootloader_dir" ipl-lba0.bin ipl-lba2.bin \
	partition-pbr.bin IO.SYS BOOT.SYS
mkdir -p "$(dirname "$output")" "$mount_dir"
truncate -s "$total_bytes" "$output"
dd if="$bootloader_dir/ipl-lba0.bin" of="$output" bs=512 count=1 \
	conv=notrunc status=none
dd if="$bootloader_dir/ipl-lba2.bin" of="$output" bs=512 seek=2 count=14 \
	conv=notrunc status=none
python3 - "$output" "$boot_last_cylinder" "$root_start_cylinder" \
	"$root_last_cylinder" "$swap_start_cylinder" \
	"$swap_last_cylinder" "$heads" "$sectors" <<'PY'
import struct
import sys

image = sys.argv[1]
boot_last = int(sys.argv[2])
root_start = int(sys.argv[3])
root_last = int(sys.argv[4])
swap_start = int(sys.argv[5])
swap_last = int(sys.argv[6])
last_head = int(sys.argv[7]) - 1
last_sector = int(sys.argv[8]) - 1


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
table[0:32] = entry(0xA1, 0x91, 1, 1, boot_last, "BOOT")
table[32:64] = entry(0x21, 0x83, root_start, root_start, root_last,
                     "DEBIAN13")
table[64:96] = entry(0x21, 0x82, swap_start, swap_start, swap_last,
                     "LINUXSWAP")
with open(image, "r+b") as stream:
    stream.seek(512)
    stream.write(table)
PY

printf '%s\n' \
	'echo Booting Debian 13 i486...' \
	'kernel VMLINUX' \
	"arg root=/dev/sda2 rootfstype=ext4 rw${kernel_extra_args:+ $kernel_extra_args}" \
	'boot' >"$cfg"

truncate -s "$root_bytes" "$root_image"
mkfs.ext4 -q -F -L DEBIAN13 "$root_image"
sudo mount -o loop "$root_image" "$mount_dir"
mounted=1
sudo cp -a "$rootfs"/. "$mount_dir"/

# The normal Debian getty starts /bin/login and its PAM stack.  On i486 PC-98
# systems that path can take long enough to time out, and at 64 MiB it may be
# killed by memory pressure before a prompt appears.  Retain getty only for
# terminal ownership/setup, then enter a root shell directly through a tiny
# wrapper which deliberately ignores any arguments supplied by getty.
sudo install -d -m 0755 "$mount_dir/usr/local/sbin"
sudo tee "$mount_dir/usr/local/sbin/pc98-direct-shell" >/dev/null <<'EOF'
#!/bin/sh
HOME=/root
USER=root
LOGNAME=root
SHELL=/bin/sh
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME USER LOGNAME SHELL PATH
cd /root || cd /
exec /bin/sh
EOF
sudo chmod 0755 "$mount_dir/usr/local/sbin/pc98-direct-shell"
sudo sed -i '/^[1-6]:[0-9]*:respawn:\/sbin\/getty /d' \
	"$mount_dir/etc/inittab"
printf '%s\n' \
	'1:2345:respawn:/sbin/getty -8 -L --noclear --skip-login --login-program /usr/local/sbin/pc98-direct-shell 38400 tty1 linux' | \
	sudo tee -a "$mount_dir/etc/inittab" >/dev/null

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

BOOT98_AUTOEXEC="$repo/bootloader/AUTOEXEC.NCT" \
DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
	"$repo/scripts/install-boot98-image.sh" --install-disk-stubs \
	"$output" "$kernel" "$cfg"
sync

sha256sum "$output"
printf 'BOOT98 Debian 13 i486 image: %s\n' "$output"
printf 'Geometry: C=%d H=%d S=%d, root=/dev/sda2, swap=/dev/sda3 (%d MiB)\n' \
	"$total_cylinders" "$heads" "$sectors" "$swap_mb"
printf 'Partition bytes: BOOT=%d, root=%d, swap=%d\n' \
	"$((boot_sectors * 512))" "$root_bytes" "$swap_bytes"
printf 'Login: root / %s\n' "$root_password"
