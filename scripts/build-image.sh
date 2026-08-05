#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

usage()
{
	cat <<'EOF'
Usage: ./build.sh image PROFILE [options]
       ./build.sh image list

Profiles:
  busybox-i386-ide      i386 BusyBox, IDE H=8/S=17, 32 MiB swap
  busybox-i386-scsi92   i386 BusyBox, 92 SCSI H=8/S=32, 32 MiB swap
  busybox-i386-scsi55   i386 BusyBox, 55 SCSI H=8/S=17, 32 MiB swap
  busybox-i386-scsi     alias for busybox-i386-scsi92
  busybox-i386-h8       i386 BusyBox, H=8/S=17, 32 MiB swap
  busybox-i486-h8       i486 kernel + BusyBox, H=8/S=17, 32 MiB swap
  debian13-i486-h8      legacy loader, Debian/i486, 128 MiB swap
  debian13-i686-h8      legacy loader, Debian/i686, 128 MiB swap
  debian13-i486-boot98  BOOT98, Debian/i486, 128 MiB swap
  debian13-i486-ide     BOOT98, Debian/i486, IDE H=8/S=17
  debian13-i486-scsi92  BOOT98, Debian/i486, 92 SCSI H=8/S=32
  debian13-i486-scsi55  BOOT98, Debian/i486, 55 SCSI H=8/S=17
  debian13-i486-scsi    alias for debian13-i486-scsi92

Options:
  --kernel FILE         use a specific uncompressed ELF kernel
  --rootfs DIR          use a specific root filesystem tree
  --base-image PATH     copy an existing raw/raw.xz image before updating it
  --base-image NAME     fetch NAME from the package-server image cache
  --output FILE         output path (must not already exist)
  --config FILE         BOOT98.CFG replacement for a BOOT98 base image
  --swap-mb N           swap size for newly created images
  --jobs N              parallel kernel/rootfs build jobs
  --publish-base NAME   cache and publish the finished image under NAME
EOF
}

list_profiles()
{
	printf '%s\n' \
		busybox-i386-ide \
		busybox-i386-scsi \
		busybox-i386-scsi92 \
		busybox-i386-scsi55 \
		busybox-i386-h8 \
		busybox-i486-h8 \
		debian13-i486-h8 \
		debian13-i686-h8 \
		debian13-i486-boot98 \
		debian13-i486-ide \
		debian13-i486-scsi \
		debian13-i486-scsi92 \
		debian13-i486-scsi55
}

materialize_path()
{
	local source="$1"
	local destination="$2"
	test ! -e "$destination" || {
		echo "Refusing to overwrite existing image: $destination" >&2
		exit 1
	}
	mkdir -p "$(dirname "$destination")"
	case "$source" in
		*.xz)
			xz -dc "$source" >"$destination.part.$$"
			mv "$destination.part.$$" "$destination"
			;;
		*)
			cp --reflink=auto --sparse=always "$source" "$destination"
			;;
	esac
}

profile="${1:-}"
if test "$profile" = list; then
	list_profiles
	exit 0
fi
test -n "$profile" || { usage >&2; exit 2; }
shift

kernel=""
rootfs=""
base_image=""
output=""
cfg=""
swap_mb=""
jobs="${JOBS:-$(nproc)}"
publish_base=""

while test "$#" -gt 0; do
	case "$1" in
		--kernel | --rootfs | --base-image | --output | --config | --swap-mb | --jobs | --publish-base)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--kernel) kernel="$2" ;;
				--rootfs) rootfs="$2" ;;
				--base-image) base_image="$2" ;;
				--output) output="$2" ;;
				--config) cfg="$2" ;;
				--swap-mb) swap_mb="$2" ;;
				--jobs) jobs="$2" ;;
				--publish-base) publish_base="$2" ;;
			esac
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		*)
			echo "Unknown image option: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

heads=8
sectors=17
kind=legacy
cpu_family=""
default_rootfs=""
default_kernel=""
default_swap=128
root_device=/dev/hd98a2
kernel_extra_args=""

case "$profile" in
	busybox-i386-h8 | busybox-i386-ide)
		kind=boot98
		cpu_family=386
		default_swap=32
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i386-scsi | busybox-i386-scsi92)
		kind=boot98
		cpu_family=386
		sectors=32
		root_device=/dev/sda2
		default_swap=32
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i386-scsi55)
		kind=boot98
		cpu_family=386
		root_device=/dev/sda2
		kernel_extra_args=pc9801_scsi=55,irq=5,dma=0,clock=12
		default_swap=32
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i486-h8)
		kind=boot98
		cpu_family=486
		default_swap=32
		default_kernel="$repo/build/i486-video/kernel/vmlinux.boot"
		;;
	debian13-i486-h8)
		default_rootfs="$repo/build/debian-i486-runtime-test/rootfs"
		default_kernel="$repo/build/kernel-7.1-i486/vmlinux.boot"
		;;
	debian13-i686-h8)
		default_rootfs="$repo/build/debian-i386-root"
		default_kernel="$repo/build/kernel-7.1/vmlinux.boot"
		;;
	debian13-i486-boot98 | debian13-i486-ide)
		kind=boot98
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.1-i486/vmlinux.boot"
		;;
	debian13-i486-scsi | debian13-i486-scsi92)
		kind=boot98
		sectors=32
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.1-i486/vmlinux.boot"
		;;
	debian13-i486-scsi55)
		kind=boot98
		kernel_extra_args=pc9801_scsi=55,irq=5,dma=0,clock=12
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.1-i486/vmlinux.boot"
		;;
	*)
		echo "Unknown image profile: $profile" >&2
		list_profiles >&2
		exit 2
		;;
esac

output="${output:-$repo/build/images/$profile.raw}"
kernel="${kernel:-$default_kernel}"
rootfs="${rootfs:-$default_rootfs}"
swap_mb="${swap_mb:-$default_swap}"

if test -n "$base_image"; then
	if test -f "$base_image"; then
		materialize_path "$base_image" "$output"
	else
		"$repo/scripts/image-cache.sh" materialize "$base_image" "$output"
	fi

	if test "$kind" = boot98; then
		DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
			"$repo/scripts/update-boot98-image.sh" \
			"$output" "$kernel" "$cfg"
	else
		DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
			"$repo/scripts/update-kernel.sh" "$output" "$kernel"
	fi
else
	case "$profile" in
		busybox-i386-h8 | busybox-i386-ide | busybox-i386-scsi | busybox-i386-scsi92 | busybox-i386-scsi55 | busybox-i486-h8)
			CPU_FAMILY="$cpu_family" I386_CONSOLE=video JOBS="$jobs" \
				ROOT_DEVICE="$root_device" \
				KERNEL_EXTRA_ARGS="$kernel_extra_args" \
				DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
				SWAP_MB="$swap_mb" OUTPUT_IMAGE="$output" \
				"$repo/scripts/build-i386-image.sh"
			if test -n "${kernel:-}" && test "$kernel" != "$default_kernel"; then
				DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
					"$repo/scripts/update-kernel.sh" "$output" "$kernel"
			fi
			;;
		debian13-i486-boot98 | debian13-i486-ide | debian13-i486-scsi | debian13-i486-scsi92 | debian13-i486-scsi55)
			SWAP_MB="$swap_mb" DISK_HEADS="$heads" \
				DISK_SECTORS="$sectors" \
				KERNEL_EXTRA_ARGS="$kernel_extra_args" \
				"$repo/scripts/make-boot98-debian-image.sh" \
				"$rootfs" "$output" "$kernel"
			;;
		debian13-i486-h8 | debian13-i686-h8)
			KERNEL_IMAGE="$kernel" ROOT_STAGE="$rootfs" \
				SWAP_MB="$swap_mb" DISK_HEADS="$heads" \
				DISK_SECTORS="$sectors" OUTPUT_IMAGE="$output" \
				"$repo/scripts/build-images.sh"
			;;
	esac
fi

if test -n "$publish_base"; then
	"$repo/scripts/image-cache.sh" publish "$publish_base" "$output"
fi

printf 'Image profile %s: %s\n' "$profile" "$output"
