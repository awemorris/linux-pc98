#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
stage="${ROOT_STAGE:-$repo/build/debian-i386-root}"
suite="${DEBIAN_SUITE:-trixie}"
mirror="${DEBIAN_MIRROR:-https://deb.debian.org/debian}"
include="${DEBIAN_INCLUDE:-sysvinit-core,e2fsprogs,kmod,udev,ifupdown,iproute2,dhcpcd-base,ca-certificates}"
root_password="${ROOT_PASSWORD:-pc98}"

if ! command -v debootstrap >/dev/null 2>&1; then
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
sudo debootstrap \
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

# Keep Debian's SysV init. Add a serial login for headless QEMU testing; the
# package-provided tty1 login remains available on the PC-98 GDC console.
printf '%s\n' \
	'' \
	'T0:23:respawn:/sbin/agetty -L ttyS0 9600 vt100' \
	| sudo tee -a "$stage/etc/inittab" >/dev/null

printf 'auto lo\niface lo inet loopback\n' \
	| sudo tee "$stage/etc/network/interfaces" >/dev/null
printf 'root:%s\n' "$root_password" | sudo chroot "$stage" chpasswd

sudo chroot "$stage" /bin/sh -c '
	command -v ip >/dev/null ||
		{ echo "iproute2 installation did not provide ip" >&2; exit 1; }
	command -v dhcpcd >/dev/null ||
		{ echo "dhcpcd-base installation did not provide dhcpcd" >&2; exit 1; }
'

echo "Debian $suite i386 rootfs staging tree: $stage"
