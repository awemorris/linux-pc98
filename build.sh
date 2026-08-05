#!/usr/bin/env bash
set -euo pipefail

repo="$(cd "$(dirname "$0")" && pwd)"

usage()
{
	cat <<'EOF'
Usage: ./build.sh COMMAND [options]

Primary commands:
  setup [options]             install Debian 13 host build/test dependencies
  bootloader                  build IPL and BOOT98 binaries
  dos-loader                  rebuild LINUX98.EXE (requires OpenWatcom 1.9)
  kernel [options]            configure and build Linux 7.1
  rootfs PROFILE              build a root filesystem
  image PROFILE [options]     create or update a named disk-image variant
  image list                  list image profiles
  test PROFILE [options]      prepare/run a headless serial-console test
  test list                   list serial test profiles
  cache fetch NAME            cache a package-server base image
  cache publish NAME FILE     publish a base image and checksum
  cache materialize NAME OUT  expand a cached base image
  cache list                  list locally cached bases

Compatibility/developer commands:
  debian                      build the default Debian rootfs/kernel/image
  dist                        compress the default legacy image
  glibc FAMILY                build glibc for i386 or i486
  glibc-tests FAMILY          build glibc tests
  glibc-image FAMILY          build a glibc validation image
  qemu-win64 COMMAND          build Windows QEMU and dependencies
  virtpc98-win64 [COMMAND]    build virtpc98.exe with PyInstaller
  win64-dist [build]          build the complete Windows ZIP distribution
  run [IMAGE]                 start qemu-pc98 with an image
  clean [current|stale]       clean active outputs or superseded build trees

Kernel options:
  --cpu 386|486|686           CPU baseline (default: 686)
  --profile pc98|full         device profile (default: pc98)
  --output-dir DIR            out-of-tree kernel build directory
  --jobs N                    parallel jobs
  --console video|dual        built-in console selection (default: video)

Run './build.sh image --help' for image-specific options.
Run './build.sh test --help' for reproducible QEMU test options.
EOF
}

build_kernel()
{
	local cpu=686 profile=pc98 output_dir="" jobs="${JOBS:-$(nproc)}"
	local console=video root_stage="${ROOT_STAGE:-}"
	while test "$#" -gt 0; do
		case "$1" in
			--cpu | --profile | --output-dir | --jobs | --console)
				test "$#" -ge 2 || { echo "Missing value for $1" >&2; exit 2; }
				case "$1" in
					--cpu) cpu="$2" ;;
					--profile) profile="$2" ;;
					--output-dir) output_dir="$2" ;;
					--jobs) jobs="$2" ;;
					--console) console="$2" ;;
				esac
				shift 2
				;;
			-h | --help)
				usage
				return
				;;
			*) echo "Unknown kernel option: $1" >&2; exit 2 ;;
		esac
	done
	case "$cpu" in
		386 | 486 | 686) ;;
		*) echo "Unsupported CPU: $cpu" >&2; exit 2 ;;
	esac
	case "$console" in
		video | dual) ;;
		*) echo "Unsupported console: $console" >&2; exit 2 ;;
	esac
	if test -z "$output_dir"; then
		case "$cpu" in
			386) output_dir="$repo/build/kernel-7.1-i386" ;;
			486) output_dir="$repo/build/kernel-7.1-i486" ;;
			686) output_dir="$repo/build/kernel-7.1" ;;
		esac
	fi
	if test -z "$root_stage"; then
		case "$cpu" in
			386) root_stage="$repo/build/i386-video/buildroot/output/target" ;;
			486) root_stage="$repo/build/boot98/debian13-i486-root" ;;
			686) root_stage="$repo/build/debian-i386-root" ;;
		esac
	fi
	CPU_FAMILY="$cpu" DEVICE_PROFILE="$profile" JOBS="$jobs" \
		CONSOLE_MODE="$console" \
		ROOT_STAGE="$root_stage" KERNEL_BUILD="$output_dir" \
		"$repo/scripts/build-kernel.sh"
}

build_rootfs()
{
	local profile="${1:-}"
	shift || true
	case "$profile" in
		debian13-i486)
			OUTPUT_DIR="${OUTPUT_DIR:-$repo/build/debian-i486-bootstrap}" \
				"$repo/debian-i486/scripts/create-public-rootfs.sh" "$@"
			;;
		debian13-i686)
			"$repo/scripts/build-debian-rootfs.sh" "$@"
			;;
		busybox-i386 | busybox-i486)
			CPU_FAMILY="${profile#busybox-i}" \
				"$repo/scripts/build-i386-rootfs.sh" "$@"
			;;
		*)
			echo "Unknown rootfs profile: $profile" >&2
			echo "Profiles: debian13-i486 debian13-i686 busybox-i386 busybox-i486" >&2
			exit 2
			;;
	esac
}

command="${1:-help}"
shift || true
case "$command" in
	help | -h | --help) usage ;;
	setup) "$repo/scripts/setup.sh" "$@" ;;
	bootloader) make -C "$repo/bootloader" "$@" ;;
	dos-loader) make -C "$repo/bootloader/dos" "$@" ;;
	kernel) build_kernel "$@" ;;
	rootfs) build_rootfs "$@" ;;
	image) "$repo/scripts/build-image.sh" "$@" ;;
	test) "$repo/scripts/test-image.sh" "$@" ;;
	cache) "$repo/scripts/image-cache.sh" "$@" ;;
	debian) "$repo/scripts/build-debian.sh" "$@" ;;
	dist) "$repo/scripts/build-dist.sh" "$@" ;;
	glibc) "$repo/scripts/build-glibc.sh" "$@" ;;
	glibc-tests) "$repo/scripts/build-glibc-tests.sh" "$@" ;;
	glibc-image) "$repo/scripts/build-glibc-validation-image.sh" "$@" ;;
	qemu-win64) "$repo/scripts/build-qemu-win64.sh" "$@" ;;
	virtpc98-win64) "$repo/scripts/build-virtpc98.sh" "$@" ;;
	win64-dist) "$repo/scripts/build-win64-dist.sh" "$@" ;;
	run) "$repo/scripts/run-qemu.sh" "$@" ;;
	clean) "$repo/scripts/clean-build.sh" "$@" ;;
	*)
		echo "Unknown command: $command" >&2
		usage >&2
		exit 2
		;;
esac
