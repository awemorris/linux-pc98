#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

usage()
{
	cat <<'EOF'
Usage: ./build.sh image PROFILE [options]
       ./build.sh image list

Profiles:
  busybox-i386-ide      Boots, i386 BusyBox, IDE H=4/S=17, below 40 MB
  busybox-i386-scsi92   Boots, i386 BusyBox, 92 SCSI H=8/S=32, 8 MiB swap
  busybox-i386-scsi55   Boots, i386 BusyBox, 55 SCSI H=8/S=17, 8 MiB swap
  debian13-i486-ide     Boots, Debian/i486, IDE H=8/S=17, 128 MiB swap
  debian13-i486-scsi92  Boots, Debian/i486, 92 SCSI H=8/S=32, 128 MiB swap
  debian13-i486-scsi55  Boots, Debian/i486, 55 SCSI H=8/S=17, 128 MiB swap

Options:
  --kernel FILE         use a specific uncompressed ELF kernel
  --rootfs DIR          use a specific root filesystem tree
  --base-image PATH     copy an existing raw/raw.xz image before updating it
  --base-image NAME     fetch NAME from the package-server image cache
  --output FILE         output path (must not already exist)
  --config FILE         BOOTS.CFG replacement for a Boots base image
  --swap-mb N           swap size for newly created images
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

image_size_for_geometry()
{
	local heads="$1" sectors="$2" cylinder_bytes total_cylinders=1 size_mb
	shift 2
	cylinder_bytes=$((heads * sectors * 512))
	for size_mb in "$@"; do
		case "$size_mb" in
			'' | *[!0-9]*) echo "Invalid partition size: $size_mb MiB" >&2; exit 2 ;;
		esac
		total_cylinders=$((total_cylinders + (size_mb * 1024 * 1024 + cylinder_bytes - 1) / cylinder_bytes))
	done
	printf '%s\n' "$((total_cylinders * cylinder_bytes))"
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
boot_mb="${BOOT_MB:-200}"
root_mb="${ROOT_MB:-200}"
jobs="${JOBS:-$(nproc)}"
publish_base=""

while test "$#" -gt 0; do
	case "$1" in
		--kernel | --rootfs | --base-image | --output | --config | --swap-mb | --boot-mb | --root-mb | --jobs | --publish-base)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--kernel) kernel="$2" ;;
				--rootfs) rootfs="$2" ;;
				--base-image) base_image="$2" ;;
				--output) output="$2" ;;
				--config) cfg="$2" ;;
				--swap-mb) swap_mb="$2" ;;
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
default_rootfs=""
default_kernel=""
default_swap=128
root_device=PARTLABEL=LINUXROOT
kernel_extra_args=""

case "$profile" in
	busybox-i386-ide)
		cpu_family=386
		boot_mb=8
		root_mb=20
		default_swap=8
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i386-scsi92)
		cpu_family=386
		sectors=32
		root_device=PARTLABEL=LINUXROOT
		kernel_extra_args="rootwait pc9801_scsi=92,mode=dma"
		boot_mb=8
		root_mb=20
		default_swap=8
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	busybox-i386-scsi55)
		cpu_family=386
		root_device=PARTLABEL=LINUXROOT
		kernel_extra_args="rootwait pc9801_scsi=55,irq=5,dma=0,clock=12,mode=async-pio"
		boot_mb=8
		root_mb=20
		default_swap=8
		default_kernel="$repo/build/i386-video/kernel/vmlinux.boot"
		;;
	debian13-i486-ide)
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.1-i486/vmlinux.boot"
		;;
	debian13-i486-scsi92)
		sectors=32
		kernel_extra_args="rootwait pc9801_scsi=92,mode=dma"
		default_rootfs="$repo/build/boot98/debian13-i486-root"
		default_kernel="$repo/build/kernel-7.1-i486/vmlinux.boot"
		;;
	debian13-i486-scsi55)
		kernel_extra_args="rootwait pc9801_scsi=55,irq=5,dma=0,clock=12,mode=async-pio"
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

if test "$profile" = busybox-i386-ide; then
	# Match the compatible BIOS small-disk geometry.  Calculate the exact
	# cylinder-rounded size that the image builder will produce with H=4.
	candidate_size="$(image_size_for_geometry 4 "$sectors" \
		"$boot_mb" "$root_mb" "$swap_mb")"
	test "$candidate_size" -ge $((40 * 1024 * 1024)) || heads=4
fi

if test -n "$base_image"; then
	if test -f "$base_image"; then
		materialize_path "$base_image" "$output"
	else
		"$repo/scripts/image-cache.sh" materialize "$base_image" "$output"
	fi

	case "$profile" in
		*-ide)
			if test "$(stat -c %s "$output")" -lt $((40 * 1024 * 1024)); then
				heads=4
			else
				heads=8
			fi
			;;
	esac

	DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
		"$repo/scripts/update-boot98-image.sh" \
		"$output" "$kernel" "$cfg"
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
				OUTPUT_IMAGE="$output"
			)
			if test -n "$rootfs"; then
				busybox_env+=(ROOT_STAGE="$rootfs" SKIP_ROOTFS_BUILD=1)
			fi
			env "${busybox_env[@]}" "$repo/scripts/build-i386-image.sh"
			if test -n "${kernel:-}" && test "$kernel" != "$default_kernel"; then
				DISK_HEADS="$heads" DISK_SECTORS="$sectors" \
					"$repo/scripts/update-kernel.sh" "$output" "$kernel"
			fi
			;;
		debian13-i486-ide | debian13-i486-scsi92 | debian13-i486-scsi55)
			SWAP_MB="$swap_mb" DISK_HEADS="$heads" \
				DISK_SECTORS="$sectors" \
				KERNEL_EXTRA_ARGS="$kernel_extra_args" \
				"$repo/scripts/make-boot98-debian-image.sh" \
				"$rootfs" "$output" "$kernel"
			;;
	esac
fi

if test -n "$publish_base"; then
	"$repo/scripts/image-cache.sh" publish "$publish_base" "$output"
fi

printf 'Image profile %s: %s\n' "$profile" "$output"
