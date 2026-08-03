#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"
kernel_version="${KERNEL_VERSION:-7.1}"
default_image="$repo/build/qemu-pc98-linux-$kernel_version.raw"
image="${OUTPUT_IMAGE:-$default_image}"
dist_dir="${DIST_DIR:-$repo/dist}"
dist_basename="${DIST_BASENAME:-qemu-pc98-debian13-i386-linux-$kernel_version}"
dist_image_name="${DIST_IMAGE_NAME:-$dist_basename.raw}"
xz_level="${XZ_LEVEL:-6}"
xz_threads="${XZ_THREADS:-0}"
archive="$dist_dir/$dist_image_name.xz"
checksum="$archive.sha256"

case "$xz_level" in
	[0-9]) ;;
	*)
		echo "XZ_LEVEL must be a single digit from 0 through 9" >&2
		exit 1
		;;
esac

if [ ! -f "$image" ]; then
	echo "Disk image not found: $image" >&2
	echo "Run KERNEL_VERSION=$kernel_version ./build-images.sh first." >&2
	exit 1
fi
if [ -e "$archive" ] || [ -e "$checksum" ]; then
	echo "Refusing to overwrite an existing dist artifact:" >&2
	echo "  $archive" >&2
	echo "  $checksum" >&2
	exit 1
fi

mkdir -p "$dist_dir"
temporary_archive="$(mktemp "$dist_dir/.${dist_basename}.raw.xz.XXXXXX")"
cleanup()
{
	rm -f "$temporary_archive"
}
trap cleanup EXIT
xz -c -T"$xz_threads" "-$xz_level" "$image" >"$temporary_archive"
mv "$temporary_archive" "$archive"
trap - EXIT
(
	cd "$dist_dir"
	sha256sum "$(basename "$archive")" >"$(basename "$checksum")"
)
chmod 0644 "$archive" "$checksum"

ls -lh "$archive" "$checksum"
