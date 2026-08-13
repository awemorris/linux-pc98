#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
. "$repo/scripts/zedbsd-env.sh"

usage()
{
	cat <<'EOF'
Usage: ./build.sh test PROFILE [options]
       ./build.sh test list

Profiles:
  busybox-i386    Linux 7.1/i386 + BusyBox, 8 MiB, serial shell
  debian13-i486   Linux 7.1/i486 + Debian 13, 64 MiB, serial login

Options:
  --image FILE       run an already prepared serial-console image
  --prepare-only     create the test image without starting QEMU
  --rebuild          discard only this profile's generated test tree first
  --jobs N           build parallelism (default: nproc)
  --memory MiB       override profile memory
  --timeout SECONDS  stop the test after this interval; zero is interactive
  --log FILE         save QEMU serial output with tee
  --qemu FILE        qemu-system-i386 binary
  --bios-dir DIR     QEMU firmware directory
  --storage TYPE     attach the supplied image as ide or scsi (default: ide)

The serial backend is connected to this terminal. The distributed video-
console images are not modified by this command.
EOF
}

list_profiles()
{
	printf '%s\n' busybox-i386 debian13-i486
}

profile="${1:-}"
if test "$profile" = list; then
	list_profiles
	exit 0
fi
case "$profile" in
	busybox-i386 | debian13-i486) ;;
	-h | --help | help | '') usage; exit 0 ;;
	*) echo "Unknown test profile: $profile" >&2; list_profiles >&2; exit 2 ;;
esac
shift

image=""
prepare_only=0
rebuild=0
jobs="${JOBS:-$(nproc)}"
timeout_seconds=0
log=""
qemu="${QEMU:-$repo/external/qemu-pc98/build/qemu-system-i386}"
bios_dir="${BIOS_DIR:-$(dirname "$qemu")/../pc-bios}"
memory=""
storage=ide

while test "$#" -gt 0; do
	case "$1" in
		--image | --jobs | --memory | --timeout | --log | --qemu | --bios-dir | --storage)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--image) image="$2" ;;
				--jobs) jobs="$2" ;;
				--memory) memory="$2" ;;
				--timeout) timeout_seconds="$2" ;;
				--log) log="$2" ;;
				--qemu) qemu="$2" ;;
				--bios-dir) bios_dir="$2" ;;
				--storage) storage="$2" ;;
			esac
			shift 2
			;;
		--prepare-only) prepare_only=1; shift ;;
		--rebuild) rebuild=1; shift ;;
		-h | --help) usage; exit 0 ;;
		*) echo "Unknown test option: $1" >&2; exit 2 ;;
	esac
done

case "$storage" in
ide | scsi) ;;
*) echo "Unsupported test storage: $storage" >&2; exit 2 ;;
esac

case "$jobs:$timeout_seconds" in
	*[!0-9:]*) echo "Jobs and timeout must be non-negative integers" >&2; exit 2 ;;
esac

work="$repo/build/tests/$profile"
if test "$rebuild" -eq 1; then
	resolved="$(realpath -m -- "$work")"
	case "$resolved" in
		"$repo/build/tests/"*) sudo rm -rf -- "$resolved" ;;
		*) echo "Refusing unsafe test path: $resolved" >&2; exit 1 ;;
	esac
fi
mkdir -p "$work"

if test -z "$image"; then
	image="$work/test.raw"
	if test ! -f "$image"; then
		case "$profile" in
			busybox-i386)
				memory="${memory:-8}"
				rootfs="$repo/build/i386-buildroot/output/target"
				skip_rootfs=0
				test -x "$rootfs/bin/busybox" && skip_rootfs=1
				CPU_FAMILY=386 I386_CONSOLE=dual JOBS="$jobs" \
					SKIP_ROOTFS_BUILD="$skip_rootfs" ROOT_STAGE="$rootfs" \
					OUTPUT_IMAGE="$image" \
					"$repo/scripts/build-i386-image.sh"
				;;
			debian13-i486)
				memory="${memory:-64}"
				kernel_build="$work/kernel"
				rootfs="$work/rootfs"
				INSTALL_MODULES=0 "$repo/build.sh" kernel --cpu 486 \
					--console dual --output-dir "$kernel_build" --jobs "$jobs"
				if test ! -d "$rootfs"; then
					source_rootfs="$repo/build/boot98/debian13-i486-root"
					test -d "$source_rootfs" || {
						echo "Debian/i486 rootfs not found: $source_rootfs" >&2
						exit 1
					}
					sudo cp -a --reflink=auto "$source_rootfs" "$rootfs"
				fi
				if ! sudo grep -q 'ttyPC0' "$rootfs/etc/inittab"; then
					printf '%s\n' \
						'T0:23:respawn:/sbin/agetty -L ttyPC0 9600 vt100' | \
						sudo tee -a "$rootfs/etc/inittab" >/dev/null
				fi
				printf '%s\n' 'auto lo' 'iface lo inet loopback' | \
					sudo tee "$rootfs/etc/network/interfaces" >/dev/null
				printf '%s\n' 'CONFIGURE_INTERFACES=no' | \
					sudo tee "$rootfs/etc/default/networking" >/dev/null
				KERNEL_IMAGE="$kernel_build/vmlinux.boot" \
					ROOT_STAGE="$rootfs" OUTPUT_IMAGE="$image" \
					ROOT_MB=512 SWAP_MB=128 BOOTLOADER=zedbsd \
					"$repo/scripts/build-images.sh"
				printf '%s\n' \
					'kernel VMLINUX' \
					'arg root=/dev/sda2 rootfstype=ext4 rw' \
					'boot' >"$work/boot.cfg"
				"$zedbsd/scripts/install-image.sh" \
					--install-disk-stubs "$image" \
					"$kernel_build/vmlinux.boot" "$work/boot.cfg"
				"$repo/bootloader/install-fs.sh" --partition 1 "$image"
				;;
		esac
	fi
fi

test -f "$image" || { echo "Test image not found: $image" >&2; exit 1; }
case "$profile" in
	busybox-i386) cpu=386; memory="${memory:-8}" ;;
	debian13-i486) cpu=486; memory="${memory:-64}" ;;
esac
test -x "$qemu" || { echo "QEMU not found: $qemu" >&2; exit 1; }
test -d "$bios_dir" || { echo "BIOS directory not found: $bios_dir" >&2; exit 1; }

printf 'Serial test image: %s\n' "$image"
printf 'QEMU: CPU=%s RAM=%s MiB (Ctrl-C to stop)\n' "$cpu" "$memory"
if test "$prepare_only" -eq 1; then
	exit 0
fi

case "$storage" in
ide) drive="if=ide,bus=0,unit=0,format=raw,file=$image,snapshot=on" ;;
scsi) drive="if=scsi,bus=0,unit=0,format=raw,file=$image,snapshot=on" ;;
esac

qemu_args=(
	-M pc9801 -cpu "$cpu" -m "$memory" -accel tcg -L "$bios_dir"
	-nic none
	-drive "$drive"
	-display none -serial stdio -no-reboot
)

# The fixed-disk IPL silently enters LBA 2.  BOOT98 automatically selects
# Auto after its three-second first-key timeout, so the headless path needs
# no synthetic keyboard input or private monitor socket.
qemu_args+=( -monitor none )
if test -n "$log"; then
	mkdir -p "$(dirname "$log")"
	if test "$timeout_seconds" -gt 0; then
		set +e
		timeout --signal=INT --kill-after=5 "$timeout_seconds" \
			"$qemu" "${qemu_args[@]}" 2>&1 | tee "$log"
		status=${PIPESTATUS[0]}
		set -e
		test "$status" -eq 124 && exit 0
		exit "$status"
	fi
	"$qemu" "${qemu_args[@]}" 2>&1 | tee "$log"
	exit "${PIPESTATUS[0]}"
fi
if test "$timeout_seconds" -gt 0; then
	set +e
	timeout --signal=INT --kill-after=5 "$timeout_seconds" \
		"$qemu" "${qemu_args[@]}"
	status=$?
	set -e
	test "$status" -eq 124 && exit 0
	exit "$status"
fi
exec "$qemu" "${qemu_args[@]}"
