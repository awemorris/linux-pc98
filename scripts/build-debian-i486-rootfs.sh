#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
stage="${ROOT_STAGE:-$repo/build/boot98/debian13-i486-root}"
suite="${DEBIAN_SUITE:-trixie}"
mirror="${DEBIAN_I486_MIRROR:-https://noctvm.io/debian-i486/packages}"
package_manifest="${DEBIAN_PACKAGE_MANIFEST:-$repo/configs/debian13-i486-packages.txt}"
xorg_cirrus_config="$repo/configs/xorg/20-pc98-coregraph.conf"
xinitrc="$repo/configs/xorg/pc98-xinitrc"
xoppai_source="$repo/demos/xoppai/xoppai.c"
xoppai_builder="$repo/scripts/build-xoppai.sh"
xoppai_output="${XOPPAI_OUTPUT:-$repo/build/xoppai/xoppai}"
debootstrap="${DEBOOTSTRAP:-$(command -v debootstrap 2>/dev/null || true)}"

test -f "$package_manifest" || {
	echo "Debian package manifest not found: $package_manifest" >&2
	exit 1
}
test -f "$xorg_cirrus_config" || {
	echo "PC-98 Core-Graph Xorg configuration not found: $xorg_cirrus_config" >&2
	exit 1
}
test -f "$xinitrc" || {
	echo "PC-98 X session startup file not found: $xinitrc" >&2
	exit 1
}
test -f "$xoppai_source" || {
	echo "xoppai source not found: $xoppai_source" >&2
	exit 1
}
test -x "$xoppai_builder" || {
	echo "xoppai builder not found: $xoppai_builder" >&2
	exit 1
}
mapfile -t packages < <(sed -e 's/[[:space:]]*#.*$//' \
	-e '/^[[:space:]]*$/d' "$package_manifest")
test "${#packages[@]}" -gt 0 || {
	echo "Debian package manifest is empty: $package_manifest" >&2
	exit 1
}
x11_profile_packages=(xorg xserver-xorg-video-cirrus xterm twm)
x11_runtime_packages=(
	xserver-xorg-core
	xserver-xorg-input-evdev
	xserver-xorg-video-cirrus
	xserver-xorg-video-fbdev
)
excluded_wayland_packages=(
	libwayland-client0
	libwayland-server0
	wayland-protocols
)
for package in "${x11_profile_packages[@]}"; do
	if ! printf '%s\n' "${packages[@]}" | grep -Fqx "$package"; then
		echo "Debian X11 rootfs profile requires $package in $package_manifest" >&2
		exit 1
	fi
done
include="${DEBIAN_INCLUDE:-$(IFS=,; echo "${packages[*]}")}"
profile_sha256="$(sha256sum "$package_manifest" "$xorg_cirrus_config" "$xinitrc" \
	"$xoppai_source" "$xoppai_builder" |
	sha256sum | awk '{print $1}')"

if test -z "$debootstrap" && test -x /usr/sbin/debootstrap; then
	debootstrap=/usr/sbin/debootstrap
fi
test -x "$debootstrap" || { echo "debootstrap is required" >&2; exit 1; }
test ! -e "$stage" || { echo "Refusing to overwrite rootfs: $stage" >&2; exit 1; }
mkdir -p "$(dirname "$stage")"
"$xoppai_builder" --output "$xoppai_output"

sudo "$debootstrap" --arch=i386 --variant=minbase --no-check-gpg \
	--include="$include" "$suite" "$stage" "$mirror"
printf 'deb [trusted=yes arch=i386] %s %s main\n' "$mirror" "$suite" |
	sudo tee "$stage/etc/apt/sources.list" >/dev/null
sudo chroot "$stage" dpkg --audit
sudo chroot "$stage" apt-get update
sudo install -D -m 0644 "$xorg_cirrus_config" \
	"$stage/etc/X11/xorg.conf.d/20-pc98-coregraph.conf"
sudo install -D -m 0755 "$xinitrc" "$stage/root/.xinitrc"
sudo install -D -m 0755 "$xoppai_output" "$stage/usr/bin/xoppai"
for package in "${packages[@]}"; do
	if ! sudo chroot "$stage" dpkg-query -W -f='${Status}\n' "$package" |
		grep -qx 'install ok installed'; then
		echo "Requested rootfs package is not installed: $package" >&2
		exit 1
	fi
done
for package in "${x11_runtime_packages[@]}"; do
	if ! sudo chroot "$stage" dpkg-query -W -f='${Status}\n' "$package" |
		grep -qx 'install ok installed'; then
		echo "Required X11 runtime package is not installed: $package" >&2
		exit 1
	fi
done
for package in "${excluded_wayland_packages[@]}"; do
	if sudo chroot "$stage" dpkg-query -W -f='${Status}\n' "$package" 2>/dev/null |
		grep -qx 'install ok installed'; then
		echo "Wayland package must not be installed in the X11 rootfs: $package" >&2
		exit 1
	fi
done
test -x "$stage/usr/lib/xorg/Xorg" || {
	echo "Xorg server executable is missing from the Debian rootfs" >&2
	exit 1
}
test -x "$stage/usr/bin/xterm" || {
	echo "xterm is missing from the Debian rootfs" >&2
	exit 1
}
test -x "$stage/usr/bin/twm" || {
	echo "twm is missing from the Debian rootfs" >&2
	exit 1
}
test -x "$stage/usr/bin/xoppai" || {
	echo "xoppai is missing from the Debian rootfs" >&2
	exit 1
}
test -f "$stage/usr/lib/xorg/modules/drivers/fbdev_drv.so" || {
	echo "Xorg fbdev driver is missing from the Debian rootfs" >&2
	exit 1
}
test -f "$stage/usr/lib/xorg/modules/drivers/cirrus_drv.so" || {
	echo "Xorg Cirrus driver is missing from the Debian rootfs" >&2
	exit 1
}
sudo cmp -s "$xorg_cirrus_config" \
	"$stage/etc/X11/xorg.conf.d/20-pc98-coregraph.conf" || {
	echo "PC-98 Core-Graph Xorg configuration was not installed correctly" >&2
	exit 1
}
sudo cmp -s "$xinitrc" "$stage/root/.xinitrc" || {
	echo "PC-98 X session startup file was not installed correctly" >&2
	exit 1
}
test -x "$stage/root/.xinitrc" || {
	echo "PC-98 X session startup file is not executable" >&2
	exit 1
}
test -f "$stage/usr/lib/xorg/modules/input/evdev_drv.so" || {
	echo "Xorg evdev driver is missing from the Debian rootfs" >&2
	exit 1
}
test -x "$stage/usr/sbin/ifconfig" || test -x "$stage/sbin/ifconfig" || {
	echo "net-tools did not install ifconfig in the Debian rootfs" >&2
	exit 1
}
sudo chroot "$stage" apt-get clean
printf '%s\n' "$profile_sha256" |
	sudo tee "$stage/etc/linux-pc98-rootfs-profile" >/dev/null
printf 'Debian %s/i486 rootfs: %s\n' "$suite" "$stage"
