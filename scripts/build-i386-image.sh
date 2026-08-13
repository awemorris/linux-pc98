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
skip_kernel_build="${SKIP_KERNEL_BUILD:-0}"
root_device="${ROOT_DEVICE:-PARTLABEL=LINUXROOT}"
swap_device="${SWAP_DEVICE:-PARTLABEL=LINUXSWAP}"
kernel_extra_args="${KERNEL_EXTRA_ARGS:-}"
image_profile="${IMAGE_PROFILE:-busybox-i${cpu_family}-ide}"

case "$console_mode" in
	dual) console_args="console=ttyPC0 console=tty0" ;;
	video) console_args="console=tty0" ;;
esac
kernel_cmdline="vdso=0 $console_args earlyprintk=pc9800 root=$root_device rootfstype=ext4 rw sysctl.vm.min_free_kbytes=64 sysctl.vm.dirty_background_bytes=32768 sysctl.vm.dirty_bytes=65536 sysctl.vm.vfs_cache_pressure=200 sysctl.vm.swappiness=100 sysctl.vm.page-cluster=0${kernel_extra_args:+ $kernel_extra_args}"

case "$root_device" in
/dev/*2) swap_device="${SWAP_DEVICE:-${root_device%2}3}" ;;
PARTLABEL=*) ;;
*) echo "ROOT_DEVICE must be /dev/...2 or PARTLABEL=...: $root_device" >&2; exit 1 ;;
esac

if [ "$skip_kernel_build" = 0 ]; then
	I386_CONSOLE="$console_mode" \
	CPU_FAMILY="$cpu_family" \
	I386_KERNEL_BUILD="$kernel_build" \
	I386_CONFIG_OUTPUT="$config_output" \
		"$repo/scripts/configure-i386-busybox.sh"
	make -C "$repo/external/kernel/linux-7.1" O="$kernel_build" ARCH=i386 -j"$jobs" vmlinux
	objcopy --strip-all "$kernel_build/vmlinux" "$boot_vmlinux"
	chmod 0644 "$boot_vmlinux"
elif [ "$skip_kernel_build" = 1 ]; then
	if [ ! -f "$boot_vmlinux" ]; then
		echo "Reusable i386 kernel is missing: $boot_vmlinux" >&2
		exit 1
	fi
else
	echo "unsupported SKIP_KERNEL_BUILD value: $skip_kernel_build" >&2
	exit 1
fi

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

# Buildroot's staging tree is shared by IDE and SCSI profiles.  Make a cheap
# temporary copy so the on-image fstab can use the matching block-device
# namespace without modifying the next profile's input tree.
image_root_stage="$(mktemp -d "${TMPDIR:-/tmp}/pc98-rootfs.XXXXXX")"
cleanup()
{
	rm -rf -- "$image_root_stage"
}
trap cleanup EXIT INT TERM
cp -a --reflink=auto "$root_stage"/. "$image_root_stage"/
cat >"$image_root_stage/etc/fstab" <<EOF
$root_device	/	ext4	defaults,noatime	0	1
$swap_device	none	swap	sw	0	0
proc	/proc	proc	defaults	0	0
sysfs	/sys	sysfs	defaults	0	0
EOF

KERNEL_VERSION=7.1 \
KERNEL_BUILD="$kernel_build" \
KERNEL_IMAGE="$boot_vmlinux" \
ROOT_STAGE="$image_root_stage" \
BOOT_MB="${BOOT_MB:-128}" \
ROOT_MB="${ROOT_MB:-200}" \
SWAP_MB="${SWAP_MB:-32}" \
SMALL_EXT4="${SMALL_EXT4:-1}" \
OUTPUT_IMAGE="$output" \
	BOOTLOADER=bootsimple \
	BOOTSIMPLE_PROFILE="$image_profile" \
	BOOTSIMPLE_CMDLINE="$kernel_cmdline" \
	"$repo/scripts/build-images.sh"
