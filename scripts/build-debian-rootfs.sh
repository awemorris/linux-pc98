#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"
suite="${DEBIAN_SUITE:-trixie}"
mirror="${DEBIAN_MIRROR:-https://deb.debian.org/debian}"
include="${DEBIAN_INCLUDE:-sysvinit-core,e2fsprogs,kmod,udev,ifupdown,iproute2,dhcpcd-base,ca-certificates}"
root_password="${ROOT_PASSWORD:-pc98}"
slim_rootfs="${SLIM_ROOTFS:-1}"
console_mode="${CONSOLE_MODE:-video}"
debootstrap="${DEBOOTSTRAP:-$(command -v debootstrap 2>/dev/null || true)}"
if [ -z "$debootstrap" ] && [ -x /usr/sbin/debootstrap ]; then
	debootstrap=/usr/sbin/debootstrap
fi

if [ -z "$debootstrap" ] || [ ! -x "$debootstrap" ]; then
	echo "debootstrap is required." >&2
	echo "On Debian, install it with: sudo apt-get install debootstrap" >&2
	exit 1
fi

if [ -e "$stage" ]; then
	echo "rootfs staging directory already exists: $stage" >&2
	echo "Set ROOT_STAGE to a new path or remove the old tree explicitly." >&2
	exit 1
fi

mkdir -p "$(dirname "$stage")"
sudo "$debootstrap" \
	--arch=i386 \
	--variant=minbase \
	--include="$include" \
	"$suite" "$stage" "$mirror"

printf 'pc98\n' | sudo tee "$stage/etc/hostname" >/dev/null
printf '%s\n' \
	'/dev/sda2 / ext4 defaults 0 1' \
	'proc /proc proc defaults 0 0' \
	'sysfs /sys sysfs defaults 0 0' \
	| sudo tee "$stage/etc/fstab" >/dev/null

# Keep Debian's package-provided tty1 login on the PC-98 GDC console. A
# serial getty is opt-in and is used only for private diagnostic images.
case "$console_mode" in
video)
	;;
dual)
	printf '%s\n' \
		'' \
		'T0:23:respawn:/sbin/agetty -L ttyPC0 9600 vt100' \
		| sudo tee -a "$stage/etc/inittab" >/dev/null
	;;
*)
	echo "Unsupported CONSOLE_MODE: $console_mode" >&2
	exit 1
	;;
esac

printf 'auto lo\niface lo inet loopback\n' \
	| sudo tee "$stage/etc/network/interfaces" >/dev/null
printf 'root:%s\n' "$root_password" | sudo chroot "$stage" chpasswd

sudo chroot "$stage" /bin/sh -c '
	command -v ip >/dev/null ||
		{ echo "iproute2 installation did not provide ip" >&2; exit 1; }
	command -v dhcpcd >/dev/null ||
		{ echo "dhcpcd-base installation did not provide dhcpcd" >&2; exit 1; }
'

if [ "$slim_rootfs" = 1 ]; then
	# Keep dpkg/apt and package copyright files usable, while omitting data
	# that can be downloaded again.  This lets the PC-98 live system fit in
	# a 200 MiB ext4 partition without turning it into an initramfs.
	sudo chroot "$stage" apt-get clean
	sudo find "$stage/var/lib/apt/lists" -mindepth 1 -delete
	sudo find "$stage/usr/share/man" -mindepth 1 -delete
	sudo find "$stage/usr/share/info" -mindepth 1 -delete
	sudo find "$stage/usr/share/locale" -mindepth 1 -delete
fi

echo "Debian $suite i386 rootfs staging tree: $stage"
