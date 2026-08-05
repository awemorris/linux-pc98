#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
console_mode="${I386_CONSOLE:-video}"
cpu_family="${CPU_FAMILY:-386}"
jobs="${JOBS:-$(nproc)}"

case "$cpu_family" in
386|486)
	;;
*)
	echo "unsupported minimal BusyBox CPU family: $cpu_family" >&2
	exit 1
	;;
esac

case "$console_mode" in
dual)
	profile="i${cpu_family}-busybox"
	default_buildroot_work="$repo/build/i386-buildroot"
	default_output="$repo/build/$profile/linux-7.1-pc98-i${cpu_family}-busybox-swap.img"
	;;
video)
	profile="i${cpu_family}-video"
	default_buildroot_work="$repo/build/i386-video/buildroot"
	default_output="$repo/build/$profile/linux-7.1-pc98-i${cpu_family}-busybox.img"
	;;
*)
	echo "unsupported I386_CONSOLE mode: $console_mode" >&2
	exit 1
	;;
esac

kernel_build="${I386_KERNEL_BUILD:-$repo/build/$profile/kernel}"
buildroot_work="${I386_BUILDROOT_WORK:-$default_buildroot_work}"
config_output="${I386_CONFIG_OUTPUT:-$repo/build/$profile/kernel.config}"
output="${OUTPUT_IMAGE:-$default_output}"
boot_vmlinux="$kernel_build/vmlinux.boot"
root_stage="${ROOT_STAGE:-$buildroot_work/output/target}"
skip_rootfs_build="${SKIP_ROOTFS_BUILD:-0}"

I386_CONSOLE="$console_mode" \
CPU_FAMILY="$cpu_family" \
I386_KERNEL_BUILD="$kernel_build" \
I386_CONFIG_OUTPUT="$config_output" \
	"$repo/scripts/configure-i386-busybox.sh"
make -C "$repo/linux-7.1" O="$kernel_build" ARCH=i386 -j"$jobs" vmlinux
objcopy --strip-all "$kernel_build/vmlinux" "$boot_vmlinux"
chmod 0644 "$boot_vmlinux"

if [ "$skip_rootfs_build" = 0 ]; then
	I386_CONSOLE="$console_mode" \
	I386_BUILDROOT_WORK="$buildroot_work" \
	JOBS="$jobs" \
		"$repo/scripts/build-i386-rootfs.sh"
elif [ "$skip_rootfs_build" != 1 ]; then
	echo "unsupported SKIP_ROOTFS_BUILD value: $skip_rootfs_build" >&2
	exit 1
fi
if [ ! -x "$root_stage/bin/busybox" ]; then
	echo "i386 BusyBox rootfs is missing: $root_stage" >&2
	exit 1
fi

# Reapply the small runtime overlay even when an existing Buildroot target is
# reused. This keeps init/profile fixes synchronized without forcing a costly
# toolchain rebuild merely to regenerate the disk image.
case "$console_mode" in
	dual) runtime_overlay="$repo/rootfs/i386" ;;
	video) runtime_overlay="$repo/rootfs/i386-video" ;;
esac
cp -a "$runtime_overlay"/. "$root_stage"/

if [ -e "$output" ]; then
	echo "Refusing to overwrite existing image: $output" >&2
	exit 1
fi

KERNEL_VERSION=7.1 \
KERNEL_BUILD="$kernel_build" \
KERNEL_IMAGE="$boot_vmlinux" \
ROOT_STAGE="$root_stage" \
BOOT_MB="${BOOT_MB:-200}" \
ROOT_MB="${ROOT_MB:-200}" \
SWAP_MB="${SWAP_MB:-32}" \
SMALL_EXT4="${SMALL_EXT4:-1}" \
OUTPUT_IMAGE="$output" \
	"$repo/scripts/build-images.sh"

cfg="$(mktemp "${TMPDIR:-/tmp}/boot98-i${cpu_family}.XXXXXX")"
trap 'rm -f "$cfg"' EXIT INT TERM
printf '%s\n' \
	"echo Booting Linux 7.1 i${cpu_family}..." \
	'kernel VMLINUX' \
	'arg root=/dev/hd98a2 rootfstype=ext4 rw' \
	'boot' >"$cfg"
DISK_HEADS="${DISK_HEADS:-8}" DISK_SECTORS="${DISK_SECTORS:-17}" \
	"$repo/scripts/install-boot98-image.sh" "$output" "$boot_vmlinux" "$cfg"
