#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

usage()
{
	cat <<'EOF'
Usage: ./build.sh image PROFILE [options]
       ./build.sh image list

Profiles:
  busybox-i386-ide      bootsimple, i386 BusyBox, IDE H=8/S=17
  busybox-i386-scsi92   bootsimple, i386 BusyBox, 92 SCSI H=8/S=32
  busybox-i386-scsi55   bootsimple, i386 BusyBox, 55 SCSI H=8/S=17
  debian13-i486-ide     bootsimple, Debian/i486, IDE H=8/S=17
  debian13-i486-scsi92  bootsimple, Debian/i486, 92 SCSI H=8/S=32
  debian13-i486-scsi55  bootsimple, Debian/i486, 55 SCSI H=8/S=17

All public images use bootsimple and a 128 MiB FAT16 BOOT partition.

Options:
  --kernel FILE         use a specific uncompressed ELF kernel
  --rootfs DIR          use a specific root filesystem tree
  --base-image PATH     copy an existing raw/raw.xz image before updating it
  --base-image NAME     fetch NAME from the package-server image cache
  --output FILE         output path (must not already exist)
  --config FILE         BOOT.CFG replacement for a zedBSD base image
  --swap-mb N           Linux swap partition size for newly created images
  --zedbsd-swapfile-mb N
                        zedBSD BOOT swapfile size (0, 32, or 64; default 64)
  --boot-mb N           FAT16 BOOT size for newly created BusyBox images
  --root-mb N           ext4 root size for newly created BusyBox images
  --jobs N              parallel kernel/rootfs build jobs
  --publish-base NAME   cache and publish the finished image under NAME
EOF
}

list_profiles()
{
	printf '%s\n' \
		busybox-i386-ide \
		busybox-i386-scsi92 \
		busybox-i386-scsi55 \
		debian13-i486-ide \
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

validate_pc98_image()
{
	python3 "$repo/scripts/mk-pc98-linux-disk.py" validate "$1" \
		--heads "$heads" --sectors "$sectors"
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
zedbsd_swapfile_mb="${ZEDBSD_SWAPFILE_MB:-}"
boot_mb="${BOOT_MB:-}"
root_mb="${ROOT_MB:-200}"
jobs="${JOBS:-$(nproc)}"
publish_base=""

while test "$#" -gt 0; do
	case "$1" in
		--kernel | --rootfs | --base-image | --output | --config | --swap-mb | --zedbsd-swapfile-mb | --boot-mb | --root-mb | --jobs | --publish-base)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--kernel) kernel="$2" ;;
				--rootfs) rootfs="$2" ;;
				--base-image) base_image="$2" ;;
				--output) output="$2" ;;
				--config) cfg="$2" ;;
				--swap-mb) swap_mb="$2" ;;
				--zedbsd-swapfile-mb) zedbsd_swapfile_mb="$2" ;;
				--boot-mb) boot_mb="$2" ;;
				--root-mb) root_mb="$2" ;;
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
cpu_family=""
image_bootloader=""
default_rootfs=""
default_kernel=""
default_swap=128
root_device=PARTLABEL=LINUXROOT
kernel_extra_args=""

case "$profile" in
	busybox-i386-ide)
		image_bootloader=bootsimple
		cpu_family=386
		root_mb=20
		default_swap=8
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i386-scsi92)
		image_bootloader=bootsimple
		cpu_family=386
		sectors=32
		root_device=PARTLABEL=LINUXROOT
		kernel_extra_args="rootwait pc9801_scsi=92,mode=dma"
		root_mb=20
		default_swap=8
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i386-scsi55)
		image_bootloader=bootsimple
		cpu_family=386
		root_device=PARTLABEL=LINUXROOT
		kernel_extra_args="rootwait pc9801_scsi=55,irq=5,dma=0,clock=12,mode=async-pio"
		root_mb=20
		default_swap=8
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	debian13-i486-ide)
		image_bootloader=bootsimple
		root_device=PARTLABEL=DEBIAN13
		kernel_extra_args="rootwait"
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.2-i486/vmlinux.boot"
		;;
	debian13-i486-scsi92)
		image_bootloader=bootsimple
		sectors=32
		root_device=PARTLABEL=DEBIAN13
		kernel_extra_args="rootwait pc9801_scsi=92,mode=dma"
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.2-i486/vmlinux.boot"
		;;
	debian13-i486-scsi55)
		image_bootloader=bootsimple
		root_device=PARTLABEL=DEBIAN13
		kernel_extra_args="rootwait pc9801_scsi=55,irq=5,dma=0,clock=12,mode=async-pio"
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.2-i486/vmlinux.boot"
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
boot_mb="${boot_mb:-128}"
bootsimple_cmdline="vdso=0 console=tty0 earlyprintk=pc9800 root=$root_device rootfstype=ext4 rw sysctl.vm.min_free_kbytes=64 sysctl.vm.dirty_background_bytes=32768 sysctl.vm.dirty_bytes=65536 sysctl.vm.vfs_cache_pressure=200 sysctl.vm.swappiness=100 sysctl.vm.page-cluster=0${kernel_extra_args:+ $kernel_extra_args}"
case "$image_bootloader" in
	bootsimple)
		zedbsd_swapfile_mb="${zedbsd_swapfile_mb:-0}"
		test -z "$cfg" || {
			echo "--config is only valid for zedBSD images" >&2
			exit 2
		}
		test "$zedbsd_swapfile_mb" = 0 || {
			echo "bootsimple images do not use a zedBSD swapfile" >&2
			exit 2
		}
		;;
	zedbsd) zedbsd_swapfile_mb="${zedbsd_swapfile_mb:-64}" ;;
esac
case "$zedbsd_swapfile_mb" in
	0 | 32 | 64) ;;
	*) echo "zedBSD swapfile size must be 0, 32, or 64 MiB" >&2; exit 2 ;;
esac

if test -n "$base_image"; then
	if test -f "$base_image"; then
		materialize_path "$base_image" "$output"
	else
		"$repo/scripts/image-cache.sh" materialize "$base_image" "$output"
	fi

	case "$profile" in
		*-ide)
			if test "$(stat -c %s "$output")" -le $((20 * 1024 * 1024)); then
				heads=4
			else
				heads=8
			fi
			;;
	esac
	# Cached and explicitly supplied base images may have been created by the
	# retired small-IDE H=4 policy.  Updating their files does not rewrite the
	# partition layout, so reject a geometry mismatch before modifying them.
	if ! validate_pc98_image "$output"; then
		rm -f -- "$output"
		exit 1
	fi

	case "$image_bootloader" in
		bootsimple)
			"$repo/bootsimple/install-image.sh" \
				--profile "$profile" --partition 1 \
				--heads "$heads" --sectors "$sectors" \
				--cmdline "$bootsimple_cmdline" \
				"$output" "$kernel"
			;;
		zedbsd)
			DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
				ZEDBSD_SWAP_SIZE_MIB="$zedbsd_swapfile_mb" \
				"$repo/scripts/update-boot98-image.sh" \
				"$output" "$kernel" "$cfg"
			;;
	esac
else
	case "$profile" in
		busybox-i386-ide | busybox-i386-scsi92 | busybox-i386-scsi55)
			busybox_env=(
				CPU_FAMILY="$cpu_family"
				I386_CONSOLE=video
				JOBS="$jobs"
				ROOT_DEVICE="$root_device"
				KERNEL_EXTRA_ARGS="$kernel_extra_args"
				DISK_HEADS="$heads"
				DISK_SECTORS="$sectors"
				BOOT_MB="$boot_mb"
				ROOT_MB="$root_mb"
				SWAP_MB="$swap_mb"
				IMAGE_PROFILE="$profile"
				OUTPUT_IMAGE="$output"
			)
			if test -n "$rootfs"; then
				busybox_env+=(ROOT_STAGE="$rootfs" SKIP_ROOTFS_BUILD=1)
			fi
			env "${busybox_env[@]}" "$repo/scripts/build-i386-image.sh"
			if test -n "${kernel:-}" && test "$kernel" != "$default_kernel"; then
				"$repo/bootsimple/install-image.sh" \
					--profile "$profile" --partition 1 \
					--heads "$heads" --sectors "$sectors" \
					--cmdline "$bootsimple_cmdline" \
					"$output" "$kernel"
			fi
			;;
		debian13-i486-ide | debian13-i486-scsi92 | debian13-i486-scsi55)
			BOOTLOADER="$image_bootloader" \
			BOOTSIMPLE_PROFILE="$profile" \
			BOOTSIMPLE_CMDLINE="$bootsimple_cmdline" \
			SWAP_MB="$swap_mb" DISK_HEADS="$heads" \
				DISK_SECTORS="$sectors" \
				ZEDBSD_SWAP_SIZE_MIB="$zedbsd_swapfile_mb" \
				KERNEL_EXTRA_ARGS="$kernel_extra_args" \
				"$repo/scripts/make-boot98-debian-image.sh" \
				"$rootfs" "$output" "$kernel"
			;;
	esac
fi

# The boot installer updates the IPL/PBR and volume contents.  Recheck the
# outer PC-98 partition table afterwards so no release can publish a truncated
# or wrong-geometry raw image.
validate_pc98_image "$output"

if test -n "$publish_base"; then
	"$repo/scripts/image-cache.sh" publish "$publish_base" "$output"
fi

printf 'Image profile %s: %s\n' "$profile" "$output"
