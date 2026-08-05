#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

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
qemu="${QEMU:-$repo/qemu-pc98/build/qemu-system-i386}"
if test ! -x "$qemu" && test -x "$HOME/qemu-pc98/build/qemu-system-i386"; then
	qemu="$HOME/qemu-pc98/build/qemu-system-i386"
fi
bios_dir="${BIOS_DIR:-$(dirname "$qemu")/../pc-bios}"
memory=""

while test "$#" -gt 0; do
	case "$1" in
		--image | --jobs | --memory | --timeout | --log | --qemu | --bios-dir)
			test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
			case "$1" in
				--image) image="$2" ;;
				--jobs) jobs="$2" ;;
				--memory) memory="$2" ;;
				--timeout) timeout_seconds="$2" ;;
				--log) log="$2" ;;
				--qemu) qemu="$2" ;;
				--bios-dir) bios_dir="$2" ;;
			esac
			shift 2
			;;
		--prepare-only) prepare_only=1; shift ;;
		--rebuild) rebuild=1; shift ;;
		-h | --help) usage; exit 0 ;;
		*) echo "Unknown test option: $1" >&2; exit 2 ;;
	esac
done

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
				ROOT_MB=512 "$repo/build.sh" image debian13-i486-h8 \
					--kernel "$kernel_build/vmlinux.boot" \
					--rootfs "$rootfs" --output "$image"
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

qemu_args=(
	-M pc9801 -cpu "$cpu" -m "$memory" -accel tcg -L "$bios_dir"
	-nic none
	-drive "if=ide,bus=0,unit=0,format=raw,file=$image,snapshot=on"
	-display none -serial stdio -no-reboot
)

monitor_dir=""
injector_pid=""
cleanup_monitor()
{
	if test -n "$injector_pid"; then
		kill "$injector_pid" 2>/dev/null || true
		wait "$injector_pid" 2>/dev/null || true
	fi
	test -z "$monitor_dir" || rm -rf -- "$monitor_dir"
}

# The generic LBA 0 IPL first asks for FDD 1 or HDD 1. BOOT98 then presents
# its third-stage menu, where Auto executes BOOT98.CFG. Inject HDD 1 followed
# by Auto over a private HMP socket before waiting for serial kernel output.
boot98_stage1="$repo/bootloader/boot98-stage1.bin"
boot98_image=0
if test "$profile" = busybox-i386 && test -f "$boot98_stage1"; then
	stage1_size="$(stat -c %s "$boot98_stage1")"
	if cmp -s "$boot98_stage1" \
		<(dd if="$image" bs=1 skip=1024 count="$stage1_size" status=none); then
		boot98_image=1
	fi
fi
if test "$boot98_image" -eq 1; then
	command -v socat >/dev/null || {
		echo "socat is required to select the BOOT98 test entry" >&2
		exit 1
	}
	monitor_dir="$(mktemp -d "$work/monitor.XXXXXX")"
	monitor_socket="$monitor_dir/hmp.sock"
	qemu_args+=( -monitor "unix:$monitor_socket,server=on,wait=off" )
	(
		for unused in $(seq 1 100); do
			test -S "$monitor_socket" && break
			sleep 0.1
		done
		test -S "$monitor_socket" || exit 1
		sleep 2
		printf 'sendkey 2\n' | socat - "UNIX-CONNECT:$monitor_socket" \
			>/dev/null
		sleep 3
		printf 'sendkey 1\n' | socat - "UNIX-CONNECT:$monitor_socket" \
			>/dev/null
	) &
	injector_pid=$!
	trap cleanup_monitor EXIT INT TERM
else
	qemu_args+=( -monitor none )
fi
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
