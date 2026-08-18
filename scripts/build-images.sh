#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
build="$repo/build"
kernel_version="${KERNEL_VERSION:-7.2}"
default_kernel_build="$build/kernel-$kernel_version"
default_output="$build/qemu-pc98-linux-$kernel_version.raw"
kernel_build="${KERNEL_BUILD:-$default_kernel_build}"
kernel_image="${KERNEL_IMAGE:-$kernel_build/vmlinux.boot}"
root_stage="${ROOT_STAGE:-$build/debian-i386-root}"
boot_mb="${BOOT_MB:-128}"
root_mb="${ROOT_MB:-200}"
swap_mb="${SWAP_MB:-0}"
small_ext4="${SMALL_EXT4:-0}"
disk_heads="${DISK_HEADS:-8}"
disk_sectors="${DISK_SECTORS:-17}"
output="${OUTPUT_IMAGE:-$default_output}"
bootloader="${BOOTLOADER:-}"
bootsimple_profile="${BOOTSIMPLE_PROFILE:-}"
bootsimple_cmdline="${BOOTSIMPLE_CMDLINE:-}"
dos_loader="${DOS_LOADER:-}"

case "$bootloader" in
	bootsimple)
		test -n "$bootsimple_profile" || {
			echo "BOOTSIMPLE_PROFILE is required" >&2; exit 2; }
		test -n "$bootsimple_cmdline" || {
			echo "BOOTSIMPLE_CMDLINE is required" >&2; exit 2; }
		;;
	zedbsd) ;;
	'') echo "BOOTLOADER=bootsimple or BOOTLOADER=zedbsd is required" >&2; exit 2 ;;
	*) echo "Unsupported BOOTLOADER: $bootloader" >&2; exit 2 ;;
esac

if [ ! -f "$kernel_image" ]; then
	echo "Kernel image not found: $kernel_image" >&2
	echo "Run ./build.sh kernel first." >&2
	exit 1
fi
if [ ! -d "$root_stage" ]; then
	echo "Rootfs staging tree not found: $root_stage" >&2
	echo "Run ./build.sh rootfs debian13-i486 first." >&2
	exit 1
fi

mkdir -p "$build"
case "$bootloader" in
	bootsimple)
		bootsimple_build="$repo/build/bootsimple/$bootsimple_profile"
		"$repo/bootsimple/build.sh" --profile "$bootsimple_profile" \
			--output-dir "$bootsimple_build" --cmdline "$bootsimple_cmdline"
		ipl="$bootsimple_build/ipl-lba0.bin"
		pbr="$bootsimple_build/partition-pbr.bin"
		;;
	zedbsd)
		. "$repo/scripts/zedbsd-env.sh"
		dos_loader="${dos_loader:-$repo/external/zedBSD/platform/pc98/dos/linux98.exe}"
		"$repo/external/zedBSD/build.sh" all pc98
		test -f "$dos_loader" || {
			echo "DOS Linux loader not found: $dos_loader" >&2
			echo "Restore it or run ./build.sh dos-loader." >&2
			exit 1
		}
		ipl="$repo/external/zedBSD/build/pc98/ipl-lba0.bin"
		pbr="$repo/external/zedBSD/build/pc98/partition-pbr.bin"
		;;
esac

image_options=(
	--boot-mb "$boot_mb"
	--root-mb "$root_mb"
	--swap-mb "$swap_mb"
	--heads "$disk_heads"
	--sectors "$disk_sectors"
)
if test "$bootloader" = zedbsd; then
	image_options+=(--dos-loader "$dos_loader")
fi
if [ "$small_ext4" != 0 ]; then
	image_options+=(--small-ext4)
fi
if test "$bootloader" = zedbsd && test -n "${BOOT_LOGO:-}"; then
	image_options+=(--logo "$BOOT_LOGO")
fi

sudo python3 "$repo/scripts/mk-pc98-linux-disk.py" create \
	"$output" \
	"$ipl" \
	"$pbr" \
	"$kernel_image" \
	"$root_stage" \
	"${image_options[@]}"
sudo chown "$(id -u):$(id -g)" "$output"

case "$bootloader" in
	bootsimple)
		"$repo/bootsimple/install-image.sh" \
			--profile "$bootsimple_profile" --partition 1 \
			--heads "$disk_heads" --sectors "$disk_sectors" \
			--cmdline "$bootsimple_cmdline" \
			"$output" "$kernel_image"
		;;
	zedbsd)
		# Debian/product images retain the complete zedBSD BOOT environment.
		DISK_HEADS="$disk_heads" DISK_SECTORS="$disk_sectors" \
			"$repo/bootloader/install-product.sh" --install-disk-stubs \
			"$output" "$kernel_image" "$repo/configs/boot.cfg"
		;;
esac

printf 'QEMU PC-98 Linux disk (%s): %s\n' "$bootloader" "$output"
