#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
stage="${ROOT_STAGE:-$repo/build/boot98/debian13-i486-root}"
suite="${DEBIAN_SUITE:-trixie}"
mirror="${DEBIAN_I486_MIRROR:-https://noctvm.io/debian-i486/packages}"
package_manifest="${DEBIAN_PACKAGE_MANIFEST:-$repo/configs/debian13-i486-packages.txt}"
debootstrap="${DEBOOTSTRAP:-$(command -v debootstrap 2>/dev/null || true)}"

test -f "$package_manifest" || {
	echo "Debian package manifest not found: $package_manifest" >&2
	exit 1
}
mapfile -t packages < <(sed -e 's/[[:space:]]*#.*$//' \
	-e '/^[[:space:]]*$/d' "$package_manifest")
test "${#packages[@]}" -gt 0 || {
	echo "Debian package manifest is empty: $package_manifest" >&2
	exit 1
}
include="${DEBIAN_INCLUDE:-$(IFS=,; echo "${packages[*]}")}"
manifest_sha256="$(sha256sum "$package_manifest" | awk '{print $1}')"

if test -z "$debootstrap" && test -x /usr/sbin/debootstrap; then
	debootstrap=/usr/sbin/debootstrap
fi
test -x "$debootstrap" || { echo "debootstrap is required" >&2; exit 1; }
test ! -e "$stage" || { echo "Refusing to overwrite rootfs: $stage" >&2; exit 1; }
mkdir -p "$(dirname "$stage")"

sudo "$debootstrap" --arch=i386 --variant=minbase --no-check-gpg \
	--include="$include" "$suite" "$stage" "$mirror"
printf 'deb [trusted=yes arch=i386] %s %s main\n' "$mirror" "$suite" |
	sudo tee "$stage/etc/apt/sources.list" >/dev/null
sudo chroot "$stage" dpkg --audit
sudo chroot "$stage" apt-get update
for package in "${packages[@]}"; do
	if ! sudo chroot "$stage" dpkg-query -W -f='${Status}\n' "$package" |
		grep -qx 'install ok installed'; then
		echo "Requested rootfs package is not installed: $package" >&2
		exit 1
	fi
done
test -x "$stage/usr/sbin/ifconfig" || test -x "$stage/sbin/ifconfig" || {
	echo "net-tools did not install ifconfig in the Debian rootfs" >&2
	exit 1
}
sudo chroot "$stage" apt-get clean
printf '%s\n' "$manifest_sha256" |
	sudo tee "$stage/etc/linux-pc98-rootfs-profile" >/dev/null
printf 'Debian %s/i486 rootfs: %s\n' "$suite" "$stage"
